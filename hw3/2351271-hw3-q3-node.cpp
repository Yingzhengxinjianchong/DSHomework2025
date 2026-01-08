#include "2351271-hw3-common.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <algorithm>
#include <random>
#include <map>
#include <atomic>

// 集群运行模式：0为Gossip 对等模式，1为Master/Leader中心化模式
int g_mode = 0; 
// 当前节点的唯一数字标识
int g_node_id = 0;
// 监听业务请求的 TCP 端口
int g_tcp_port = 0;
// 监听心跳交换的 UDP 端口
int g_udp_port = 0;
// 内存中维护的当前集群所有活跃成员映射表
std::map<int, MemberEntry> g_members;
// 墓碑列表：记录最近移除节点的ID和移除时间，防止由于UDP延迟导致的节点伪复活
std::map<int, uint64_t> g_dead_nodes; 
// 保护成员列表的多线程互斥锁
std::mutex g_member_mutex;
// 存储在本地的Block数据索引映射表
std::map<uint32_t, BlockIndexEntry> g_index_map;
// 标志位：控制后台服务线程的运行与优雅停止
std::atomic<bool> g_running(true);

// 实用函数：通过TCP套接字循环接收数据，直到填满指定长度的缓冲区
bool receiveAll(int sock, void* buffer, size_t size) {
    char* ptr = (char*)buffer;
    while (size > 0) {
        ssize_t n = recv(sock, ptr, size, 0);
        if (n <= 0) return false;
        ptr += n;
        size -= n;
    }
    return true;
}

// 启动预加载：从磁盘索引文件中恢复Block元数据并统计数量
void loadLocalIndex() {
    std::string idx_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".idx";
    std::ifstream ifs(idx_name, std::ios::binary);
    if (!ifs) {
        std::cout << "[Node " << g_node_id << "] 提示：未检测到历史索引文件，将以空库状态启动。" << std::endl;
        return;
    }
    BlockIndexEntry entry;
    while (ifs.read(reinterpret_cast<char*>(&entry), sizeof(BlockIndexEntry))) {
        g_index_map[entry.block_id] = entry;
    }
    ifs.close();
    std::cout << "[Node " << g_node_id << "] 索引恢复成功，本地已托管 " << g_index_map.size() << " 个 Block 副本。" << std::endl;
}

// 处理Q1请求：接收来自客户端的1MB数据块并进行磁盘持久化存储
void handleInsert(int sock, uint32_t payload_size) {
    InsertHeader ih;
    // 接收包含块ID的头部信息
    receiveAll(sock, &ih, sizeof(InsertHeader));
    uint32_t d_size = payload_size - sizeof(InsertHeader);
    std::vector<char> buf(d_size);
    // 接收实际的数据载荷
    receiveAll(sock, buf.data(), d_size);
    // 以追加模式打开对应的.dat 数据文件
    std::string dat_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".dat";
    std::ofstream df(dat_name, std::ios::binary | std::ios::app);
    // 移动文件指针到末尾以获取插入位置的偏移量
    df.seekp(0, std::ios::end);
    uint64_t off = df.tellp();
    df.write(buf.data(), d_size);
    df.close();
    // 构造三元组索引条目并持久化到.idx 文件
    BlockIndexEntry entry = { ih.block_id, off, d_size };
    std::string idx_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".idx";
    std::ofstream iff(idx_name, std::ios::binary | std::ios::app);
    iff.write(reinterpret_cast<char*>(&entry), sizeof(BlockIndexEntry));
    iff.close();
    // 同步更新内存中的索引快速映射表
    g_index_map[ih.block_id] = entry;
    // 打印符合Q1要求的详细存储日志
    std::cout << "[节点 " << g_node_id << "] 存储成功：Block " << ih.block_id 
              << " | 大小: " << formatSize(d_size) << " | 文件偏移: " << off << std::endl;
}

// 处理Q2请求：根据学号定位数据块并解析返回成绩记录
void handleQuery(int sock) {
    QueryRequest req;
    receiveAll(sock, &req, sizeof(QueryRequest));
    // 根据记录索引计算其应当所在的 Block ID
    uint32_t target_bid = (req.student_id - 1) / RECORDS_PER_BLOCK;
    QueryResponse resp = {0};
    // 查找本地索引确定数据是否存在
    if (g_index_map.count(target_bid)) {
        BlockIndexEntry idx = g_index_map[target_bid];
        std::ifstream df(STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".dat", std::ios::binary);
        if (df.is_open()) {
            // 计算记录在数据块内的相对字节偏移
            int r_idx = (req.student_id - 1) % RECORDS_PER_BLOCK;
            df.seekg(idx.offset + (r_idx * PACKED_RECORD_SIZE));
            // 读取原始打包记录
            if (df.read(reinterpret_cast<char*>(&resp.record), PACKED_RECORD_SIZE)) {
                // 校验返回的 ID 以确保索引定位准确
                if (resp.record.id == req.student_id) resp.success = 1;
            }
            df.close();
        }
    }
    // 向客户端发送包含成功标志和数据的响应包
    send(sock, &resp, sizeof(QueryResponse), 0);
}

// 处理Q3获取成员列表请求：将当前在线节点名单发回客户端
void handleGetMembers(int sock) {
    std::lock_guard<std::mutex> lock(g_member_mutex);
    GossipPacket pkt;
    pkt.count = 0;
    // 遍历成员映射表，构造定长的网络传输包
    for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
        if (pkt.count < MAX_MEMBERS) pkt.members[pkt.count++] = it->second;
    }
    send(sock, &pkt, sizeof(GossipPacket), 0);
}

// TCP线程逻辑：持续监听业务端口并分发处理不同的协议消息
void tcpServerThread() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr = { AF_INET, htons((uint16_t)g_tcp_port), {INADDR_ANY} };
    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_sock, 10);
    // 设置非阻塞超时，允许线程响应g_running停止信号
    struct timeval tv = {1, 0};
    setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    while (g_running) {
        int client_sock = accept(server_sock, nullptr, nullptr);
        if (client_sock < 0) continue;
        MsgHeader h;
        if (receiveAll(client_sock, &h, sizeof(MsgHeader))) {
            if (h.type == MsgType::INSERT_BLOCK) handleInsert(client_sock, h.payload_size);
            else if (h.type == MsgType::QUERY_STUDENT) handleQuery(client_sock);
            else if (h.type == MsgType::GET_MEMBERS) handleGetMembers(client_sock);
        }
        close(client_sock);
    }
    close(server_sock);
}

// UDP接收线程逻辑：接收心跳包并根据模式（Gossip 或 Master）合并状态
void udpReceiverThread() {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr = { AF_INET, htons((uint16_t)g_udp_port), {INADDR_ANY} };
    bind(udp_sock, (struct sockaddr*)&addr, sizeof(addr));
    struct timeval tv = {1, 0};
    setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    while (g_running) {
        GossipPacket pkt;
        ssize_t n = recvfrom(udp_sock, &pkt, sizeof(pkt), 0, nullptr, nullptr);
        if (n <= 0) continue;
        std::lock_guard<std::mutex> lock(g_member_mutex);
        uint64_t now = Timer::now_ms();
        for (uint32_t i = 0; i < pkt.count; ++i) {
            int mid = pkt.members[i].id;
            if (mid == g_node_id) continue;
            // 收到主动注销信号 (心跳设为最大值 0xFFFFFFFF)
            if (pkt.members[i].heartbeat == 0xFFFFFFFF) {
                if (g_members.count(mid)) {
                    std::cout << "[GMS] 收到节点 " << mid << " 的告别通知，立即注销。" << std::endl;
                    g_members.erase(mid);
                    g_dead_nodes[mid] = now; // 记录移除时刻到墓碑列表
                }
                continue;
            }
            // 墓碑过滤检查：若节点最近刚被移除（2秒内），则忽略过时的旧包防止“僵尸复活”
            if (g_dead_nodes.count(mid)) {
                if (now - g_dead_nodes[mid] < 2000) continue; 
                else g_dead_nodes.erase(mid); // 冷却时间过后清除墓碑
            }
            // 合并新成员或更新已有成员的心跳数
            if (g_members.find(mid) == g_members.end()) {
                std::cout << "[GMS] 发现新节点 " << mid << " 加入集群。" << std::endl;
                g_members[mid] = pkt.members[i];
                g_members[mid].last_seen = now;
            } else if (pkt.members[i].heartbeat > g_members[mid].heartbeat) {
                g_members[mid].heartbeat = pkt.members[i].heartbeat;
                g_members[mid].last_seen = now;
            }
        }
    }
    close(udp_sock);
}

// 分布式心跳扩散线程：执行故障检测并根据模式扩散列表，确保带宽开销低
void gossipSenderThread() {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    std::mt19937 rng(time(0));
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::lock_guard<std::mutex> lock(g_member_mutex);
        uint64_t now = Timer::now_ms();
        // 增加本节点的心跳计数
        g_members[g_node_id].heartbeat++;
        g_members[g_node_id].last_seen = now;
        // 故障检测逻辑：若超过5秒未收到心跳更新，则认为节点宕机
        for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ) {
            if (it->first != g_node_id && (now - it->second.last_seen > 5000)) {
                std::cout << "[GMS] 节点 " << it->first << " 疑似故障，已移除。" << std::endl;
                g_dead_nodes[it->first] = now; // 同时存入墓碑防止网络延迟干扰
                g_members.erase(it++);
            } else ++it;
        }
        // 准备待扩散的成员数据包
        GossipPacket pkt;
        pkt.count = 0;
        for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
            if (pkt.count < MAX_MEMBERS) pkt.members[pkt.count++] = it->second;
        }
        // 区分模式执行扩散逻辑
        if (g_mode == 1) { // 方式i：简单的Master/Leader模式
            if (g_node_id == 0) { // 如果我是Master，我向当前在线的所有普通节点同步全局视图
                for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
                    if (it->first == 0) continue;
                    sockaddr_in dest = { AF_INET, htons((uint16_t)(it->first + 9000)), {inet_addr("127.0.0.1")} };
                    sendto(udp_sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&dest, sizeof(dest));
                }
            } else { // 如果我不是Master，我只将我的视图或存活信息发送给 Master (8000)
                sockaddr_in m_addr = { AF_INET, htons(9000), {inet_addr("127.0.0.1")} };
                sendto(udp_sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&m_addr, sizeof(m_addr));
            }
        } else { // 方式ii：Gossip对等扩散模式
            std::vector<int> others;
            for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
                if (it->first != g_node_id) others.push_back(it->first);
            }
            if (!others.empty()) {
                std::shuffle(others.begin(), others.end(), rng);
                // 随机选择最多2个活跃邻居进行扩散，确保全网开销为O(N)
                for (int i = 0; i < std::min((int)others.size(), 2); ++i) {
                    sockaddr_in dest = { AF_INET, htons((uint16_t)(others[i] + 9000)), {inet_addr("127.0.0.1")} };
                    sendto(udp_sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&dest, sizeof(dest));
                }
            }
        }
    }
    close(udp_sock);
}

int main(int argc, char* argv[]) {
    // 启动参数：./node <端口> [模式:0-Gossip, 1-Master]
    if (argc < 2) { std::cerr << "用法: " << argv[0] << " <监听端口> [模式: 0-Gossip, 1-Master]" << std::endl; return -1; }
    g_tcp_port = std::stoi(argv[1]);
    if (argc >= 3) g_mode = std::stoi(argv[2]);
    g_node_id = g_tcp_port - 8000;
    g_udp_port = g_node_id + 9000;
    // 环境初始化
    loadLocalIndex();
    MemberEntry self = { g_node_id, (uint32_t)inet_addr("127.0.0.1"), (uint16_t)g_tcp_port, 0, Timer::now_ms() };
    g_members[g_node_id] = self;
    // Introducer机制：非种子节点通过8000端口进入组服务
    if (g_tcp_port != 8000) {
        GossipPacket join_pkt = { 1, {self} };
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in seed = { AF_INET, htons(9000), {inet_addr("127.0.0.1")} };
        sendto(sock, &join_pkt, sizeof(join_pkt), 0, (struct sockaddr*)&seed, sizeof(seed));
        close(sock);
    }
    // 启动所有协议服务线程
    std::thread t1(tcpServerThread);
    std::thread t2(udpReceiverThread);
    std::thread t3(gossipSenderThread);
    std::cout << ">>> 节点 " << g_node_id << " 启动模式: " << (g_mode == 1 ? "Master" : "Gossip") << "。输入 'exit' 主动退出。" << std::endl;
    // 主线程监听控制台输入，实现主动离开流程
    std::string cmd;
    while (std::cin >> cmd) {
        if (cmd == "exit") {
            std::cout << "[GMS] 正在通知集群成员并准备安全下线..." << std::endl;
            g_running = false; 
            self.heartbeat = 0xFFFFFFFF; // 标记告别信号
            GossipPacket lp = { 1, {self} };
            int ls = socket(AF_INET, SOCK_DGRAM, 0);
            std::lock_guard<std::mutex> lock(g_member_mutex);
            // 主动下线时通知所有已知邻居以实现即时感知
            for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
                if (it->first == g_node_id) continue;
                sockaddr_in d = { AF_INET, htons((uint16_t)(it->first + 9000)), {inet_addr("127.0.0.1")} };
                sendto(ls, &lp, sizeof(lp), 0, (struct sockaddr*)&d, sizeof(d));
            }
            close(ls);
            break;
        }
    }
    // 回收所有系统线程
    t1.join(); t2.join(); t3.join();
    std::cout << ">>> 节点服务已停止。" << std::endl;
    return 0;
}