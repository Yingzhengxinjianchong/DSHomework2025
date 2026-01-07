#include "2351271-hw3-common.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <iomanip>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Q2: 存储节点
// 自动映射 ID：根据端口映射为编号。
// 预加载索引：启动时将对应的 .idx 文件内容读入内存 map。
// 随机访问：收到查询请求后，通过 fseek 瞬间定位 .dat 文件。

std::map<uint32_t, BlockIndexEntry> g_index_map;
int g_node_id = 0;

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

// 加载本地索引
void loadLocalIndex() {
    std::string idx_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".idx";
    std::ifstream ifs(idx_name, std::ios::binary);
    if (!ifs) {
        std::cout << "[Node " << g_node_id << "] 警告：未找到索引文件 " << idx_name << std::endl;
        return;
    }

    BlockIndexEntry entry;
    int count = 0;
    while (ifs.read(reinterpret_cast<char*>(&entry), sizeof(BlockIndexEntry))) {
        g_index_map[entry.block_id] = entry;
        count++;
    }
    ifs.close();
    std::cout << "[Node " << g_node_id << "] 索引加载完毕，共管理 " << count << " 个数据块。" << std::endl;
}

void handleQuery(int client_sock) {
    QueryRequest req;
    if (!receiveAll(client_sock, &req, sizeof(QueryRequest))) return;

    uint32_t target_block = (req.student_id - 1) / RECORDS_PER_BLOCK;
    QueryResponse resp;
    resp.success = 0;

    // 检查本地索引
    if (g_index_map.count(target_block)) {
        BlockIndexEntry idx = g_index_map[target_block];
        std::string dat_name = STUDENT_ID + "-hw3-" + std::to_string(g_node_id) + ".dat";
        std::ifstream df(dat_name, std::ios::binary);
        
        if (df.is_open()) {
            // 计算记录在文件中的绝对偏移量
            int record_idx_in_block = (req.student_id - 1) % RECORDS_PER_BLOCK;
            uint64_t seek_pos = idx.offset + (record_idx_in_block * PACKED_RECORD_SIZE);
            
            df.seekg(seek_pos);
            if (df.read(reinterpret_cast<char*>(&resp.record), sizeof(PackedStudentRecord))) {
                if (resp.record.id == req.student_id) resp.success = 1;
            }
            df.close();
        }
    }
    send(client_sock, &resp, sizeof(QueryResponse), 0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <端口号>" << std::endl;
        return -1;
    }

    int port = std::stoi(argv[1]);
    // 自动生成节点编号
    g_node_id = (port >= 8000 && port < 9000) ? (port - 8000) : port;

    loadLocalIndex();

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr = { AF_INET, htons((uint16_t)port), {INADDR_ANY} };
    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("绑定失败");
        return -1;
    }

    listen(server_sock, 10);
    std::cout << ">>> 存储节点 " << g_node_id << " 查询服务已启动，监听端口 " << port << std::endl;

    while (true) {
        int client_sock = accept(server_sock, nullptr, nullptr);
        MsgHeader header;
        if (receiveAll(client_sock, &header, sizeof(MsgHeader))) {
            if (header.type == MsgType::QUERY_STUDENT) handleQuery(client_sock);
        }
        close(client_sock);
    }
    return 0;
}