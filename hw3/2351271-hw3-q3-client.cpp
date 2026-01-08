#include "2351271-hw3-common.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <random>

// 缓存当前集群的活跃成员列表
std::vector<MemberEntry> g_active_members;

// 获取成员列表：根据作业要求，此逻辑兼容两种模式
// 在实现方式 i (Master模式) 下，seed_port 必须是 Master 节点端口
// 在实现方式 ii (Gossip模式) 下，seed_port 可以是集群中任意已知的“引荐人”端口
bool fetchMembership(int seed_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = { AF_INET, htons((uint16_t)seed_port), {inet_addr("127.0.0.1")} };
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { 
        close(sock); 
        return false; 
    }
    // 发送 GET_MEMBERS 请求获取全网视图
    MsgHeader h = { MsgType::GET_MEMBERS, 0 };
    send(sock, &h, sizeof(MsgHeader), 0);
    // 接收包含成员数量和明细的数据包
    GossipPacket pkt;
    recv(sock, &pkt, sizeof(pkt), MSG_WAITALL);
    close(sock);
    // 更新本地缓存
    g_active_members.clear();
    for (uint32_t i = 0; i < pkt.count; ++i) g_active_members.push_back(pkt.members[i]);
    return true;
}

// 分块插入逻辑：基于动态感知到的活节点进行双副本分发
void runInsert() {
    std::ifstream ifs(STUDENT_ID + "-hw2.dat1", std::ios::binary);
    if (!ifs) { 
        std::cout << "错误: 找不到原始数据源文件。" << std::endl; 
        return; 
    }
    std::vector<PackedStudentRecord> buffer(RECORDS_PER_BLOCK);
    uint32_t bid = 0;
    std::mt19937 rng(time(0));
    // 循环读取数据源并按 1MB 大小进行逻辑切片
    while (ifs.read(reinterpret_cast<char*>(buffer.data()), RECORDS_PER_BLOCK * PACKED_RECORD_SIZE) || ifs.gcount() > 0) {
        size_t b_read = ifs.gcount();
        size_t r_read = b_read / PACKED_RECORD_SIZE;
        if (r_read == 0) break;
        // 在当前所有感知的活节点中随机选择存储位置
        std::uniform_int_distribution<int> dist(0, g_active_members.size() - 1);
        int n = dist(rng);
        int m = (n + 1) % g_active_members.size();
        // 打印 Q1 风格的路由与大小信息
        std::cout << "[Block " << bid << "] 大小: " << formatSize(b_read) 
                  << " -> 路由至: Node " << g_active_members[n].id 
                  << " (主) & Node " << g_active_members[m].id << " (副本)" << std::endl;
        // 针对主副本和备副本并行发送 TCP 存储请求
        auto sender = [&](int idx, uint32_t block_id) {
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
        sender(n, bid); sender(m, bid);
        bid++;
    }
}

// 分布式查询逻辑：跨节点自动寻址与结果展示
void runQuery() {
    while (true) {
        std::cout << "\n请输入要查询的学号 (输入 0 退出): ";
        int sid; std::cin >> sid; if (sid <= 0) break;
        Timer t; bool found = false;
        // 轮询集群中每一个活着的节点，直至查找到结果或遍历结束
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
                // 如果查询成功，打印详细的 Q2 风格成绩明细
                if (resp.success) {
                    double ms = t.interval();
                    std::cout << "---------------------------------------" << std::endl;
                    std::cout << "查询成功！耗时: " << ms << " ms" << std::endl;
                    std::cout << "所在节点: ID " << g_active_members[i].id << std::endl;
                    std::cout << "学号: " << resp.record.id << std::endl;
                    std::cout << "语文: " << std::fixed << std::setprecision(1) << resp.record.chinese_x10 / 10.0 << std::endl;
                    std::cout << "数学: " << std::fixed << std::setprecision(1) << resp.record.math_x10 / 10.0 << std::endl;
                    std::cout << "英语: " << std::fixed << std::setprecision(1) << resp.record.english_x10 / 10.0 << std::endl;
                    std::cout << "综合: " << std::fixed << std::setprecision(1) << resp.record.composite_x10 / 10.0 << std::endl;
                    std::cout << "---------------------------------------" << std::endl;
                    found = true; close(s); break;
                }
            }
            close(s);
        }
        if (!found) std::cout << "在当前在线节点中未检索到该记录。" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    // 客户端启动参数：必须提供一个已知的服务器端口（Master 或 Introducer）
    if (argc < 2) { 
        std::cout << "用法: " << argv[0] << " <服务节点端口>" << std::endl; 
        return -1; 
    }
    int seed_port = std::stoi(argv[1]);
    // 首次拉取集群拓扑结构
    if (!fetchMembership(seed_port)) { 
        std::cout << "连接失败，请确保目标节点在线。" << std::endl; 
        return -1; 
    }
    std::cout << "已通过成员服务获取拓扑，当前活跃节点数: " << g_active_members.size() << std::endl;
    std::cout << "模式选择：1. 插入数据  2. 查询数据" << std::endl << "请输入: ";
    int choice; std::cin >> choice;
    if (choice == 1) runInsert(); else runQuery();
    return 0;
}