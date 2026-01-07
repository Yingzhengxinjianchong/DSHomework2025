#ifndef HW3_COMMON_H_2351271
#define HW3_COMMON_H_2351271

#include <string>
#include <chrono>
#include <cstdint>
#include <vector>
#include <iostream>
#include <netinet/in.h>
#include <iomanip>
#include <sstream>
#include <map>
#include <mutex>

// 包含共享数据结构、网络协议以及通用工具函数

// --- 常量定义 ---
const std::string STUDENT_ID = "2351271";
const int NUM_RECORDS = 512 * 32 * 8;

// --- 学生记录结构 ---
struct StudentRecord {
    int id;
    float chinese;
    float math;
    float english;
    float composite;
};

#pragma pack(push, 1)
struct PackedStudentRecord {
    int id;
    uint16_t chinese_x10;
    uint16_t math_x10;
    uint16_t english_x10;
    uint16_t composite_x10;
};
#pragma pack(pop)

const int PACKED_RECORD_SIZE = sizeof(PackedStudentRecord);
const int RECORDS_PER_BLOCK = 1048576 / PACKED_RECORD_SIZE;

// --- 网络消息协议 ---
// 消息类型
enum class MsgType : uint8_t {
    INSERT_BLOCK = 1,      
    QUERY_STUDENT = 2,     
    GOSSIP_HEARTBEAT = 3,
    GET_MEMBERS = 4,
    LEAVE_GROUP = 5
};

#pragma pack(push, 1)
// 通用消息头
struct MsgHeader {
    MsgType type;
    uint32_t payload_size; 
};

// 插入操作头
struct InsertHeader {
    uint32_t block_id;
};

// 索引条目结构
struct BlockIndexEntry {
    uint32_t block_id;
    uint64_t offset;
    uint32_t size;
};

// 查询请求
struct QueryRequest {
    int student_id;
};

// 查询响应
struct QueryResponse {
    uint8_t success; 
    PackedStudentRecord record;
};
#pragma pack(pop)

// --- 组成员协议 (UDP Gossip) ---
const int MAX_MEMBERS = 32;     
const int GOSSIP_PORT = 8000;   

#pragma pack(push, 1)
// GMS 成员条目
struct MemberEntry {
    int id;              // 节点 ID
    uint32_t ip;         // IP 地址 (网络字节序)
    uint16_t port;       // 端口 (主机字节序)
    uint32_t heartbeat;  // 心跳计数
    uint64_t last_seen;  // 本地收到的最后一次心跳时间 (ms)
};

// Gossip 数据包
struct GossipPacket {
    uint32_t count;
    MemberEntry members[MAX_MEMBERS];
};
#pragma pack(pop)

// --- 通用工具类与函数 ---

// 格式化字节大小输出 (inline 防止多重定义错误)
inline std::string formatSize(size_t bytes) {
    if (bytes >= 1024 * 1024) {
        double mb = bytes / (1024.0 * 1024.0);
        return std::to_string(mb).substr(0, std::to_string(mb).find(".") + 3) + " MB";
    } else {
        double kb = bytes / 1024.0;
        return std::to_string(kb).substr(0, std::to_string(kb).find(".") + 3) + " KB";
    }
}

// 计时器类
class Timer {
public:
    Timer() { start_time = std::chrono::steady_clock::now(); }
    void reset() { start_time = std::chrono::steady_clock::now(); }
    double interval() {
        auto end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> diff = end_time - start_time;
        return diff.count();
    }
    // 获取当前毫秒级系统时间戳
    static uint64_t now_ms() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }
private:
    std::chrono::steady_clock::time_point start_time;
};

#endif // HW3_COMMON_H_2351271