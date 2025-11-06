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

#endif