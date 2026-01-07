#include "2351271-hw3-common.h"
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

// Q2: 客户端
// 向提供的所有节点发送查询请求，直到找到结果。

bool queryNode(int port, int student_id, QueryResponse& out_resp) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    // 设置接收超时，防止节点挂掉导致无限等待
    struct timeval tv;
    tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    sockaddr_in addr = { AF_INET, htons((uint16_t)port), {inet_addr("127.0.0.1")} };
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }

    MsgHeader h = { MsgType::QUERY_STUDENT, sizeof(QueryRequest) };
    QueryRequest req = { student_id };
    send(sock, &h, sizeof(MsgHeader), 0);
    send(sock, &req, sizeof(QueryRequest), 0);

    ssize_t n = recv(sock, &out_resp, sizeof(QueryResponse), MSG_WAITALL);
    close(sock);
    return (n == sizeof(QueryResponse) && out_resp.success == 1);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <节点1端口> <节点2端口> ..." << std::endl;
        return -1;
    }

    std::vector<int> node_ports;
    for (int i = 1; i < argc; ++i) node_ports.push_back(std::stoi(argv[i]));

    std::cout << "========== Q2 分布式查询终端 ==========" << std::endl;

    while (true) {
        std::cout << "\n请输入要查询的学号 (1-131072, 输入0退出): ";
        int sid;
        if (!(std::cin >> sid) || sid <= 0) break;

        Timer timer;
        bool found = false;
        QueryResponse res;

        for (int port : node_ports) {
            if (queryNode(port, sid, res)) {
                found = true;
                break;
            }
        }

        double ms = timer.interval();

        if (found) {
            std::cout << "---------------------------------------" << std::endl;
            std::cout << "查询成功！耗时: " << ms << " ms" << std::endl;
            std::cout << "学号: " << res.record.id << std::endl;
            std::cout << "语文: " << std::fixed << std::setprecision(1) << res.record.chinese_x10 / 10.0 << std::endl;
            std::cout << "数学: " << res.record.math_x10 / 10.0 << std::endl;
            std::cout << "英语: " << res.record.english_x10 / 10.0 << std::endl;
            std::cout << "综合: " << res.record.composite_x10 / 10.0 << std::endl;
            std::cout << "---------------------------------------" << std::endl;
        } else {
            std::cout << "在所有在线节点中均未找到该记录。耗时: " << ms << " ms" << std::endl;
        }
    }
    return 0;
}