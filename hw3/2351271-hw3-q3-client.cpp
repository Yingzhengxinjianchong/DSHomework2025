#include "2351271-hw3-common.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <random>

// 活跃成员快照
std::vector<MemberEntry> g_active_members;

// 拉取成员列表
bool fetchMembership(int seed_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = { AF_INET, htons((uint16_t)seed_port), {inet_addr("127.0.0.1")} };
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(sock); return false; }
    MsgHeader h = { MsgType::GET_MEMBERS, 0 };
    send(sock, &h, sizeof(MsgHeader), 0);
    GossipPacket pkt;
    recv(sock, &pkt, sizeof(pkt), MSG_WAITALL);
    close(sock);
    g_active_members.clear();
    for (uint32_t i = 0; i < pkt.count; ++i) g_active_members.push_back(pkt.members[i]);
    return true;
}

// 分块插入逻辑
void runInsert() {
    std::ifstream ifs(STUDENT_ID + "-hw2.dat1", std::ios::binary);
    if (!ifs) return;
    std::vector<PackedStudentRecord> buffer(RECORDS_PER_BLOCK);
    uint32_t bid = 0;
    std::mt19937 rng(time(0));
    while (ifs.read(reinterpret_cast<char*>(buffer.data()), RECORDS_PER_BLOCK * PACKED_RECORD_SIZE) || ifs.gcount() > 0) {
        size_t b_read = ifs.gcount();
        size_t r_read = b_read / PACKED_RECORD_SIZE;
        if (r_read == 0) break;
        std::uniform_int_distribution<int> dist(0, g_active_members.size() - 1);
        int n = dist(rng);
        int m = (n + 1) % g_active_members.size();
        // 打印Q1风格插入日志
        std::cout << "[Block " << bid << "] 大小: " << formatSize(b_read) 
                  << " -> 路由至: Node " << g_active_members[n].id 
                  << " (主) & Node " << g_active_members[m].id << " (副本)" << std::endl;
        auto send_fn = [&](int idx, uint32_t block_id) {
            int s = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in d = { AF_INET, htons(g_active_members[idx].port), {g_active_members[idx].ip} };
            if (connect(s, (struct sockaddr*)&d, sizeof(d)) >= 0) {
                MsgHeader mh = { MsgType::INSERT_BLOCK, (uint32_t)(sizeof(InsertHeader) + b_read) };
                InsertHeader ih = { block_id };
                send(s, &mh, sizeof(MsgHeader), 0);
                send(s, &ih, sizeof(InsertHeader), 0);
                send(s, buffer.data(), b_read, 0);
            }
            close(s);
        };
        send_fn(n, bid); send_fn(m, bid);
        bid++;
    }
}

// 学号查询逻辑
void runQuery() {
    while (true) {
        std::cout << "\n请输入学号查询 (0退出): ";
        int sid; std::cin >> sid; if (sid <= 0) break;
        Timer t; bool found = false;
        for (size_t i = 0; i < g_active_members.size(); ++i) {
            int s = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in d = { AF_INET, htons(g_active_members[i].port), {g_active_members[i].ip} };
            if (connect(s, (struct sockaddr*)&d, sizeof(d)) >= 0) {
                MsgHeader mh = { MsgType::QUERY_STUDENT, sizeof(QueryRequest) };
                QueryRequest req = { sid };
                send(s, &mh, sizeof(MsgHeader), 0);
                send(s, &req, sizeof(QueryRequest), 0);
                QueryResponse resp;
                recv(s, &resp, sizeof(QueryResponse), MSG_WAITALL);
                if (resp.success) {
                    double ms = t.interval();
                    // 打印Q2风格查询日志
                    std::cout << "---------------------------------------" << std::endl;
                    std::cout << "查询成功！耗时: " << ms << " ms" << std::endl;
                    std::cout << "学号: " << resp.record.id << std::endl;
                    std::cout << "语文: " << std::fixed << std::setprecision(1) << resp.record.chinese_x10 / 10.0 << std::endl;
                    std::cout << "数学: " << resp.record.math_x10 / 10.0 << std::endl;
                    std::cout << "英语: " << resp.record.english_x10 / 10.0 << std::endl;
                    std::cout << "综合: " << resp.record.composite_x10 / 10.0 << std::endl;
                    std::cout << "---------------------------------------" << std::endl;
                    found = true; close(s); break;
                }
            }
            close(s);
        }
        if (!found) std::cout << "未在该集群节点中检索到记录。" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) return -1;
    if (!fetchMembership(std::stoi(argv[1]))) return -1;
    std::cout << "活跃节点数: " << g_active_members.size() << std::endl;
    std::cout << "1.插入数据 2.查询记录: ";
    int choice; std::cin >> choice;
    if (choice == 1) runInsert(); else runQuery();
    return 0;
}