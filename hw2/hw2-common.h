#ifndef HW2_COMMON_H
#define HW2_COMMON_H

#include <string>
#include <chrono>
#include <cstdint>
#include <iostream>

// 学号
const std::string STUDENT_ID = "2351271";

// 文件名
const std::string TXT_FILE = STUDENT_ID + "-hw2.txt";
const std::string DAT1_FILE = STUDENT_ID + "-hw2.dat1";
const std::string DAT2_FILE = STUDENT_ID + "-hw2.dat2";
const std::string IDX_FILE = STUDENT_ID + "-hw2.idx";

// 记录总数
const int NUM_RECORDS = 512 * 32 * 8;

// 学生记录
struct StudentRecord {
  int id;
  float chinese;
  float math;
  float english;
  float composite;
};

// 用于文件写入/读取的紧凑数据结构
#pragma pack(push, 1) // 强制1字节对齐
struct PackedStudentRecord {
  int id;
  uint16_t chinese_x10;
  uint16_t math_x10;
  uint16_t english_x10;
  uint16_t composite_x10;
};
#pragma pack(pop) // 恢复默认对齐

// 计时器
class Timer {
public:
  Timer() {
    start_time = std::chrono::steady_clock::now();
  }

  void reset() {
    start_time = std::chrono::steady_clock::now();
  }

  double interval() {
    std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::duration duration = end_time - start_time;

    // double
    std::chrono::duration<double, std::milli> duration_ms = duration;
    return duration_ms.count();
  }
private:
  std::chrono::steady_clock::time_point start_time;
};

// 索引树的常量
const int NUM_SCORES = 101;
const int NUM_LEAVES = NUM_SCORES;
const int NUM_INTERNAL_NODES = NUM_LEAVES - 1;
const int TOTAL_NODES = NUM_LEAVES + NUM_INTERNAL_NODES;

/* C/S 网络协议 */
// 服务器端口
const int SERVER_PORT = 23512;

// 客户端→服务器的请求包
#pragma pack(push, 1)
struct QueryRequest {
  uint8_t type; // 1 = 任务1.5, 2 = 任务1.6
  float f_param1; // 任务1.5: (float)ID, 任务1.6: score_low
  float f_param2; // 任务1.5: 不用, 任务1.6: score_high
};
#pragma pack(pop)

// 服务器→客户端的响应包
#pragma pack(push, 1)
struct QueryResponse {
  // 11 = 1.5 成功
  // 12 = 1.6 成功
  // 255 = 错误
  uint8_t type;

  // 16 字节的数据联合体
  union {
    // 任务 1.5 响应
    float scores[4]; // [语, 数, 英, 综]

    // 任务 1.6 响应
    struct {
      int count;
      double average;
    } score_result;
    
  } data;
};
#pragma pack(pop)

#endif