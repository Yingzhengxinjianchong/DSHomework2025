#include "2351271-hw3-common.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Q1: 存储节点
// 作为一个 TCP 服务器运行，监听客户端连接。
// 根据监听端口自动生成节点编号 (例如 8001 对应 1, 8002 对应 2)。
// 接收 MsgHeader，识别出这是一个“插入数据块”的操作。
// 接收 InsertHeader，获取这个数据块的唯一编号 (Block ID)。
// 记录该数据块在文件中的起始偏移量，并将 <块ID, 偏移量, 大小> 存入 .idx 索引文件。

// 从Socket接收指定长度的数据
bool receiveAll(int sock, void* buffer, size_t size) {
    char* ptr = (char*)buffer;
    while (size > 0) {
        ssize_t n = recv(sock, ptr, size, 0);
        if (n <= 0) return false; // 连接断开或出错
        ptr += n;
        size -= n;
    }
    return true;
}

/**
 * 处理数据块插入的核心函数
 * @param client_sock 客户端 Socket 连接
 * @param payload_size MsgHeader 中声明的总载荷大小
 * @param node_id 自动生成的节点编号 (1, 2, 3...)
 */
void handleInsertBlock(int client_sock, uint32_t payload_size, int node_id) {
    // 接收插入请求头，获取Block ID
    InsertHeader ins_header;
    if (!receiveAll(client_sock, &ins_header, sizeof(InsertHeader))) return;

    // 接收实际的学生数据载荷
    uint32_t data_size = payload_size - sizeof(InsertHeader);
    std::vector<char> buffer(data_size);
    if (!receiveAll(client_sock, buffer.data(), data_size)) return;

    // 将数据写入本地数据文件.dat (追加模式)
    std::string dat_name = STUDENT_ID + "-hw3-" + std::to_string(node_id) + ".dat";
    std::ofstream dat_file(dat_name, std::ios::binary | std::ios::app);
    
    // 获取当前文件末尾位置作为指针
    dat_file.seekp(0, std::ios::end);
    uint64_t offset = dat_file.tellp();
    
    dat_file.write(buffer.data(), data_size);
    dat_file.close();

    // 将对应的索引信息存入.idx文件
    std::string idx_name = STUDENT_ID + "-hw3-" + std::to_string(node_id) + ".idx";
    std::ofstream idx_file(idx_name, std::ios::binary | std::ios::app);
    
    BlockIndexEntry entry;
    entry.block_id = ins_header.block_id;
    entry.offset = offset;
    entry.size = data_size;
    
    idx_file.write(reinterpret_cast<const char*>(&entry), sizeof(BlockIndexEntry));
    idx_file.close();

    std::cout << "[节点 " << node_id << "] 存储成功：Block " << ins_header.block_id 
              << " | 大小: " << formatSize(data_size) 
              << " | 本地偏移: " << offset << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <端口号>" << std::endl;
        std::cerr << "示例: " << argv[0] << " 8001" << std::endl;
        return -1;
    }

    int port = std::stoi(argv[1]);    

    // 自动生成节点编号
    int node_id = (port >= 8000 && port < 9000) ? (port - 8000) : port;

    // 创建TCP Socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("绑定失败");
        return -1;
    }

    listen(server_sock, 10);
    std::cout << ">>> 存储节点 " << node_id << " 启动成功 (由端口 " << port << " 自动映射)" << std::endl;
    std::cout << ">>> 正在监听端口 " << port << "..." << std::endl;

    while (true) {
        sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &len);
        if (client_sock < 0) continue;

        MsgHeader header;
        if (receiveAll(client_sock, &header, sizeof(MsgHeader))) {
            if (header.type == MsgType::INSERT_BLOCK) {
                handleInsertBlock(client_sock, header.payload_size, node_id);
            }
        }
        close(client_sock);
    }

    close(server_sock);
    return 0;
}