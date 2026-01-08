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

// 节点状态全局管理
int g_node_id = 0;
int g_tcp_port = 0;
int g_udp_port = 0;
std::map<int, MemberEntry> g_members;
std::map<int, uint64_t> g_dead_nodes; 
std::mutex g_member_mutex;
std::map<uint32_t, BlockIndexEntry> g_index_map;
std::atomic<bool> g_running(true);

// 可靠接收数据
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

// 启动时加载已有索引
void loadLocalIndex() {
    std::string idx_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".idx";
    std::ifstream ifs(idx_name, std::ios::binary);
    if (!ifs) {
        std::cout << "[Node " << g_node_id << "] 提示：未找到历史索引文件。" << std::endl;
        return;
    }
    BlockIndexEntry entry;
    while (ifs.read(reinterpret_cast<char*>(&entry), sizeof(BlockIndexEntry))) {
        g_index_map[entry.block_id] = entry;
    }
    ifs.close();
    std::cout << "[Node " << g_node_id << "] 索引加载完毕，包含 " << g_index_map.size() << " 个数据块。" << std::endl;
}

// 处理数据插入
void handleInsert(int sock, uint32_t payload_size) {
    InsertHeader ih;
    receiveAll(sock, &ih, sizeof(InsertHeader));
    uint32_t d_size = payload_size - sizeof(InsertHeader);
    std::vector<char> buf(d_size);
    receiveAll(sock, buf.data(), d_size);
    // 写入数据
    std::string dat_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".dat";
    std::ofstream df(dat_name, std::ios::binary | std::ios::app);
    df.seekp(0, std::ios::end);
    uint64_t off = df.tellp();
    df.write(buf.data(), d_size);
    df.close();
    // 写入索引
    BlockIndexEntry entry = { ih.block_id, off, d_size };
    std::string idx_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".idx";
    std::ofstream iff(idx_name, std::ios::binary | std::ios::app);
    iff.write(reinterpret_cast<char*>(&entry), sizeof(BlockIndexEntry));
    iff.close();
    g_index_map[ih.block_id] = entry;
    // 打印Q1风格日志
    std::cout << "[节点 " << g_node_id << "] 存储成功：Block " << ih.block_id << " | 大小: " << formatSize(d_size) << " | 本地偏移: " << off << std::endl;
}

// 处理学号查询
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

// 处理获取成员请求
void handleGetMembers(int sock) {
    std::lock_guard<std::mutex> lock(g_member_mutex);
    GossipPacket pkt;
    pkt.count = 0;
    for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
        if (pkt.count < MAX_MEMBERS) pkt.members[pkt.count++] = it->second;
    }
    send(sock, &pkt, sizeof(GossipPacket), 0);
}

// TCP 线程处理业务数据
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

// UDP 线程接收成员心跳
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
            // 收到注销信号立即移除并记录墓碑
            if (pkt.members[i].heartbeat == 0xFFFFFFFF) {
                if (g_members.count(mid)) {
                    std::cout << "[GMS] 收到节点 " << mid << " 的告别包，立即移除。" << std::endl;
                    g_members.erase(mid);
                    g_dead_nodes[mid] = now;
                }
                continue;
            }
            // 墓碑检查
            if (g_dead_nodes.count(mid)) {
                if (now - g_dead_nodes[mid] < 2000) continue; 
                else g_dead_nodes.erase(mid);
            }
            // 感知加入
            if (g_members.find(mid) == g_members.end()) {
                std::cout << "[GMS] 感知到新节点 " << mid << " 加入集群。" << std::endl;
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

// Gossip 扩散与故障扫描
void gossipSenderThread() {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    std::mt19937 rng(time(0));
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::lock_guard<std::mutex> lock(g_member_mutex);
        uint64_t now = Timer::now_ms();
        g_members[g_node_id].heartbeat++;
        g_members[g_node_id].last_seen = now;
        // 故障检测日志
        for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ) {
            if (it->first != g_node_id && (now - it->second.last_seen > 5000)) {
                std::cout << "[GMS] 节点 " << it->first << " 疑似故障，已移除。" << std::endl;
                g_dead_nodes[it->first] = now; 
                g_members.erase(it++);
            } else ++it;
        }
        GossipPacket pkt;
        pkt.count = 0;
        for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
            if (pkt.count < MAX_MEMBERS) pkt.members[pkt.count++] = it->second;
        }
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
    // 启动即加载索引
    loadLocalIndex();
    MemberEntry self = { g_node_id, (uint32_t)inet_addr("127.0.0.1"), (uint16_t)g_tcp_port, 0, Timer::now_ms() };
    g_members[g_node_id] = self;
    if (g_tcp_port != 8000) {
        GossipPacket join_pkt = { 1, {self} };
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in seed = { AF_INET, htons(9000), {inet_addr("127.0.0.1")} };
        sendto(sock, &join_pkt, sizeof(join_pkt), 0, (struct sockaddr*)&seed, sizeof(seed));
        close(sock);
    }
    std::thread t1(tcpServerThread);
    std::thread t2(udpReceiverThread);
    std::thread t3(gossipSenderThread);
    std::cout << ">>> 节点 " << g_node_id << " 启动成功。输入 'exit' 主动退出集群。" << std::endl;
    std::string cmd;
    while (std::cin >> cmd) {
        if (cmd == "exit") {
            std::cout << "[GMS] 正在通知邻居并主动下线..." << std::endl;
            g_running = false; 
            self.heartbeat = 0xFFFFFFFF;
            GossipPacket lp = { 1, {self} };
            int ls = socket(AF_INET, SOCK_DGRAM, 0);
            std::lock_guard<std::mutex> lock(g_member_mutex);
            for (std::map<int, MemberEntry>::iterator it = g_members.begin(); it != g_members.end(); ++it) {
                if (it->first != g_node_id) {
                    sockaddr_in dest = { AF_INET, htons((uint16_t)(it->first + 9000)), {inet_addr("127.0.0.1")} };
                    sendto(ls, &lp, sizeof(lp), 0, (struct sockaddr*)&dest, sizeof(dest));
                }
            }
            close(ls);
            break;
        }
    }
    t1.join(); t2.join(); t3.join();
    return 0;
}