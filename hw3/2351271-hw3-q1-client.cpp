#include "2351271-hw3-common.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <random>
#include <ctime>

// Q1: 客户端
// 读取作业2生成的数据文件 2351271-hw2.dat1。
// 按照每块 104857 条记录（约 1MB）进行分块读取，防止记录被切断。
// 对于每一块，随机选择一个主节点端口n。
// 按照“双副本”要求，将该块同时发送给节点n和节点(n+1) mod N。

// 将一个数据块通过TCP发送给指定节点
bool sendBlock(int port, uint32_t block_id, const std::vector<PackedStudentRecord>& data) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // 建立连接
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }

    uint32_t data_bytes = data.size() * sizeof(PackedStudentRecord);
    
    // 通用消息头MsgHeader（告知类型和总数据长度）
    MsgHeader msg_h;
    msg_h.type = MsgType::INSERT_BLOCK;
    msg_h.payload_size = sizeof(InsertHeader) + data_bytes;

    // 构造插入请求头（告知 Block ID）
    InsertHeader ins_h;
    ins_h.block_id = block_id;

    // 顺序发送：[MsgHeader] -> [InsertHeader] -> [数据载荷]
    send(sock, &msg_h, sizeof(MsgHeader), 0);
    send(sock, &ins_h, sizeof(InsertHeader), 0);
    send(sock, data.data(), data_bytes, 0);

    close(sock);
    return true;
}

int main(int argc, char* argv[]) {
    // 通过命令行参数获取所有在线的节点端口
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <节点1端口> <节点2端口> ..." << std::endl;
        return -1;
    }

    std::vector<int> node_ports;
    for (int i = 1; i < argc; ++i) {
        node_ports.push_back(std::stoi(argv[i]));
    }

    // 打开作业2的数据源
    std::string filename = STUDENT_ID + "-hw2.dat1";
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) {
        std::cerr << "错误：找不到作业 2 数据文件 " << filename << std::endl;
        std::cerr << "请确保该文件已拷贝到当前运行目录。" << std::endl;
        return -1;
    }

    std::cout << "========== 客户端：分布式数据插入开始 ==========" << std::endl;
    Timer timer; // 开始计时
    
    // 缓冲区，大小为 104857 条记录
    std::vector<PackedStudentRecord> buffer(RECORDS_PER_BLOCK);
    uint32_t block_count = 0;
    
    // 初始化随机数生成器
    std::mt19937 rng(static_cast<unsigned int>(time(nullptr)));
    std::uniform_int_distribution<int> dist(0, (int)node_ports.size() - 1);

    // 循环读取文件，直到结束
    while (ifs.read(reinterpret_cast<char*>(buffer.data()), RECORDS_PER_BLOCK * sizeof(PackedStudentRecord)) || ifs.gcount() > 0) {
        size_t records_read = ifs.gcount() / sizeof(PackedStudentRecord);
        if (records_read == 0) break;

        // 截取当前读取到的实际数据块
        std::vector<PackedStudentRecord> current_data(buffer.begin(), buffer.begin() + records_read);
        size_t current_bytes = current_data.size() * sizeof(PackedStudentRecord);

        // 随机选择主存储节点n
        int n_idx = dist(rng);
        int primary_port = node_ports[n_idx];
        // 选择备份节点(n+1) mod N
        int replica_port = node_ports[(n_idx + 1) % node_ports.size()];

        std::cout << "[Block " << block_count << "] 大小: " << formatSize(current_bytes) 
                  << " -> 路由至端口: " << primary_port << " (主) & " << replica_port << " (副本)" << std::endl;

        // 执行双副本发送
        if (!sendBlock(primary_port, block_count, current_data)) {
            std::cerr << "警告: 无法连接至节点 " << primary_port << std::endl;
        }
        if (!sendBlock(replica_port, block_count, current_data)) {
            std::cerr << "警告: 无法连接至副本节点 " << replica_port << std::endl;
        }

        block_count++;
    }

    std::cout << "================================================" << std::endl;
    std::cout << "插入任务完成。总分块数: " << block_count 
              << " | 总耗时: " << timer.interval() << " ms" << std::endl;

    return 0;
}