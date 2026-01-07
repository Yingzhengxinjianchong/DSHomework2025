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

// 节点运行状态全局变量
int g_node_id = 0;
// TCP 监听端口
int g_tcp_port = 0;
// UDP Gossip 端口
int g_udp_port = 0;
// 活跃成员列表映射表
std::map<int, MemberEntry> g_members;
// 墓碑列表：记录最近移除的节点 ID 和时间戳，防止残留包导致复活
std::map<int, uint64_t> g_dead_nodes; 
// 保护成员列表的互斥锁
std::mutex g_member_mutex;
// 本地数据块索引
std::map<uint32_t, BlockIndexEntry> g_index_map;
// 线程运行控制标志
std::atomic<bool> g_running(true);

// 循环读取 Socket 确保获取完整数据包
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

// 处理来自客户端的数据块存储请求
void handleInsert(int sock, uint32_t payload_size) {
    InsertHeader ih;
    receiveAll(sock, &ih, sizeof(InsertHeader));
    uint32_t d_size = payload_size - sizeof(InsertHeader);
    std::vector<char> buf(d_size);
    receiveAll(sock, buf.data(), d_size);
    std::string dat_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".dat";
    std::ofstream df(dat_name, std::ios::binary | std::ios::app);
    df.seekp(0, std::ios::end);
    uint64_t off = df.tellp();
    df.write(buf.data(), d_size);
    df.close();
    BlockIndexEntry entry = { ih.block_id, off, d_size };
    std::string idx_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".idx";
    std::ofstream iff(idx_name, std::ios::binary | std::ios::app);
    iff.write(reinterpret_cast<char*>(&entry), sizeof(BlockIndexEntry));
    iff.close();
    g_index_map[ih.block_id] = entry;
}

// 处理基于索引的数据查询请求
void handleQuery(int sock) {
    QueryRequest req;
    receiveAll(sock, &req, sizeof(QueryRequest));
    uint32_t target_bid = (req.student_id - 1) / RECORDS_PER_BLOCK;
    QueryResponse resp = {0};
    if (g_index_map.count(target_bid)) {
        BlockIndexEntry idx = g_index_map[target_bid];
        std::ifstream df(STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".dat", std::ios::binary);
        if (df.is_open()) {
            int r_idx = (req.student_id - 1) % RECORDS_PER_BLOCK;
            df.seekg(idx.offset + (r_idx * PACKED_RECORD_SIZE));
            df.read(reinterpret_cast<char*>(&resp.record), PACKED_RECORD_SIZE);
            if (resp.record.id == req.student_id) resp.success = 1;
        }
    }
    send(sock, &resp, sizeof(QueryResponse), 0);
}

// 获取当前所有活跃节点的成员列表
void handleGetMembers(int sock) {
    std::lock_guard<std::mutex> lock(g_member_mutex);
    GossipPacket pkt;
    pkt.count = 0;
    for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
        if (pkt.count < MAX_MEMBERS) pkt.members[pkt.count++] = it->second;
    }
    send(sock, &pkt, sizeof(GossipPacket), 0);
}

// TCP 服务主线程处理业务逻辑
void tcpServerThread() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr = { AF_INET, htons((uint16_t)g_tcp_port), {INADDR_ANY} };
    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_sock, 10);
    struct timeval tv;
    tv.tv_sec = 1; tv.tv_usec = 0;
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

// UDP 接收线程合并来自其他节点的心跳信息
void udpReceiverThread() {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr = { AF_INET, htons((uint16_t)g_udp_port), {INADDR_ANY} };
    bind(udp_sock, (struct sockaddr*)&addr, sizeof(addr));
    struct timeval tv;
    tv.tv_sec = 1; tv.tv_usec = 0;
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
            // 处理主动离开信号 0xFFFFFFFF
            if (pkt.members[i].heartbeat == 0xFFFFFFFF) {
                if (g_members.count(mid)) {
                    std::cout << "[GMS] 收到节点 " << mid << " 的告别包，立即移除。" << std::endl;
                    g_members.erase(mid);
                    g_dead_nodes[mid] = now; // 加入墓碑列表防止复活
                }
                continue;
            }
            // 墓碑检查逻辑：如果该节点最近刚刚移除（2秒内），则忽略过时的旧包
            if (g_dead_nodes.count(mid)) {
                if (now - g_dead_nodes[mid] < 2000) continue; 
                else g_dead_nodes.erase(mid); // 超过 2 秒则允许新加入
            }
            // 全新节点加入逻辑
            if (g_members.find(mid) == g_members.end()) {
                std::cout << "[GMS] 感知到新节点 " << mid << " 加入集群。" << std::endl;
                g_members[mid] = pkt.members[i];
                g_members[mid].last_seen = now;
            } else if (pkt.members[i].heartbeat > g_members[mid].heartbeat) {
                // 更新心跳计数器和观测时间
                g_members[mid].heartbeat = pkt.members[i].heartbeat;
                g_members[mid].last_seen = now;
            }
        }
    }
    close(udp_sock);
}

// Gossip 周期性发送心跳及检测节点故障
void gossipSenderThread() {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    std::mt19937 rng(time(0));
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::lock_guard<std::mutex> lock(g_member_mutex);
        uint64_t now = Timer::now_ms();
        g_members[g_node_id].heartbeat++;
        g_members[g_node_id].last_seen = now;
        // 检测超过 5 秒未收到心跳的故障节点
        for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ) {
            if (it->first != g_node_id && (now - it->second.last_seen > 5000)) {
                std::cout << "[GMS] 节点 " << it->first << " 疑似故障，已移除。" << std::endl;
                g_dead_nodes[it->first] = now; // 存入墓碑列表，时间设为 2 秒冷却
                g_members.erase(it++);
            } else ++it;
        }
        GossipPacket pkt;
        pkt.count = 0;
        for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
            if (pkt.count < MAX_MEMBERS) pkt.members[pkt.count++] = it->second;
        }
        // 向集群内随机挑选两个节点发送最新视图
        if (g_members.size() > 1) {
            std::vector<int> others;
            for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
                if (it->first != g_node_id) others.push_back(it->first);
            }
            std::shuffle(others.begin(), others.end(), rng);
            int targets = std::min((int)others.size(), 2);
            for (int i = 0; i < targets; ++i) {
                sockaddr_in dest = { AF_INET, htons((uint16_t)(others[i] + 9000)), {inet_addr("127.0.0.1")} };
                sendto(udp_sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&dest, sizeof(dest));
            }
        }
    }
    close(udp_sock);
}

int main(int argc, char* argv[]) {
    if (argc < 2) return -1;
    g_tcp_port = std::stoi(argv[1]);
    g_node_id = g_tcp_port - 8000;
    g_udp_port = g_node_id + 9000;
    // 启动时读取本地已有的数据索引
    std::string idx_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".idx";
    std::ifstream ifs(idx_name, std::ios::binary);
    if (ifs) {
        BlockIndexEntry entry;
        while (ifs.read(reinterpret_cast<char*>(&entry), sizeof(BlockIndexEntry))) g_index_map[entry.block_id] = entry;
        ifs.close();
    }
    // 初始化节点自身信息
    MemberEntry self = { g_node_id, (uint32_t)inet_addr("127.0.0.1"), (uint16_t)g_tcp_port, 0, Timer::now_ms() };
    g_members[g_node_id] = self;
    // 种子节点机制：非 8000 节点启动即加入
    if (g_tcp_port != 8000) {
        GossipPacket join_pkt = { 1, {self} };
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in seed = { AF_INET, htons(9000), {inet_addr("127.0.0.1")} };
        sendto(sock, &join_pkt, sizeof(join_pkt), 0, (struct sockaddr*)&seed, sizeof(seed));
        close(sock);
    }
    // 开启三个功能线程
    std::thread t1(tcpServerThread);
    std::thread t2(udpReceiverThread);
    std::thread t3(gossipSenderThread);
    std::cout << ">>> 节点 " << g_node_id << " 启动就绪。输入 'exit' 主动退出集群。" << std::endl;
    std::string cmd;
    while (std::cin >> cmd) {
        if (cmd == "exit") {
            std::cout << "[GMS] 正在通知邻居并主动下线..." << std::endl;
            g_running = false; 
            self.heartbeat = 0xFFFFFFFF; // 发送特殊告别包
            GossipPacket leave_pkt = { 1, {self} };
            int leave_sock = socket(AF_INET, SOCK_DGRAM, 0);
            std::lock_guard<std::mutex> lock(g_member_mutex);
            for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
                if (it->first != g_node_id) {
                    sockaddr_in dest = { AF_INET, htons((uint16_t)(it->first + 9000)), {inet_addr("127.0.0.1")} };
                    sendto(leave_sock, &leave_pkt, sizeof(leave_pkt), 0, (struct sockaddr*)&dest, sizeof(dest));
                }
            }
            close(leave_sock);
            break;
        }
    }
    t1.join(); t2.join(); t3.join();
    return 0;
}