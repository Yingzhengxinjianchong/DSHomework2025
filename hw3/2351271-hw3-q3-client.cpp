#include "2351271-hw3-common.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <random>

// Q3 Client: 具备服务发现能力的集成客户端

std::vector<MemberEntry> g_active_members;

// 向任意已知节点索取最新的成员列表
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

// 数据插入逻辑
void runInsert() {
    std::ifstream ifs(STUDENT_ID + "-hw2.dat1", std::ios::binary);
    if (!ifs) return;
    std::vector<PackedStudentRecord> buffer(RECORDS_PER_BLOCK);
    uint32_t bid = 0;
    std::mt19937 rng(time(0));
    while (ifs.read(reinterpret_cast<char*>(buffer.data()), RECORDS_PER_BLOCK * PACKED_RECORD_SIZE) || ifs.gcount() > 0) {
        size_t cnt = ifs.gcount() / PACKED_RECORD_SIZE;
        if (cnt == 0) break;
        std::uniform_int_distribution<int> dist(0, g_active_members.size() - 1);
        int n = dist(rng);
        int m = (n + 1) % g_active_members.size();

        // 针对 n 节点和 m 节点发送 TCP 插入请求
        auto send_task = [&](int idx, uint32_t b_id) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in dest = { AF_INET, htons(g_active_members[idx].port), {g_active_members[idx].ip} };
            if (connect(sock, (struct sockaddr*)&dest, sizeof(dest)) >= 0) {
                uint32_t d_bytes = cnt * PACKED_RECORD_SIZE;
                MsgHeader mh = { MsgType::INSERT_BLOCK, (uint32_t)(sizeof(InsertHeader) + d_bytes) };
                InsertHeader ih = { b_id };
                send(sock, &mh, sizeof(MsgHeader), 0);
                send(sock, &ih, sizeof(InsertHeader), 0);
                send(sock, buffer.data(), d_bytes, 0);
            }
            close(sock);
        };
        send_task(n, bid); send_task(m, bid);
        std::cout << "Block " << bid << " 分发成功 (节点 " << g_active_members[n].id << " & " << g_active_members[m].id << ")" << std::endl;
        bid++;
    }
}

// 数据查询逻辑
void runQuery() {
    while (true) {
        std::cout << "\n输入学号查询 (0退出): ";
        int sid; std::cin >> sid; if (sid <= 0) break;
        Timer t; bool found = false;
        // 遍历所有在线成员直到查到结果
        for (auto const& m : g_active_members) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in dest = { AF_INET, htons(m.port), {m.ip} };
            if (connect(sock, (struct sockaddr*)&dest, sizeof(dest)) >= 0) {
                MsgHeader mh = { MsgType::QUERY_STUDENT, sizeof(QueryRequest) };
                QueryRequest req = { sid };
                send(sock, &mh, sizeof(MsgHeader), 0);
                send(sock, &req, sizeof(QueryRequest), 0);
                QueryResponse resp;
                recv(sock, &resp, sizeof(QueryResponse), MSG_WAITALL);
                if (resp.success) {
                    std::cout << "找到记录！学号: " << resp.record.id << " 耗时: " << t.interval() << " ms" << std::endl;
                    found = true; close(sock); break;
                }
            }
            close(sock);
        }
        if (!found) std::cout << "未找到记录。" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) { std::cout << "用法: " << argv[0] << " <种子端口>" << std::endl; return -1; }
    int seed = std::stoi(argv[1]);
    if (!fetchMembership(seed)) { std::cout << "无法连接到种子节点。" << std::endl; return -1; }
    std::cout << "集群在线节点数: " << g_active_members.size() << std::endl;
    
    std::cout << "选择功能: 1.插入数据 2.查询数据: ";
    int choice; std::cin >> choice;
    if (choice == 1) runInsert(); else runQuery();
    return 0;
}