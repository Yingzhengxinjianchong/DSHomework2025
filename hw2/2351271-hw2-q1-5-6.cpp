#include "hw2-common.h"
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>
#include <climits>
#include <algorithm>

/* 任务1.5：根据学号查询dat1 */
void queryByID()
{
  int target_id;
  std::string input_line;

  std::cout << "\n[任务1.5：按学号查询]" << std::endl;
  
  while (true) {
    std::cout << "请输入学号( 1 - " << NUM_RECORDS << " )，或输入exit回到主菜单: ";
    std::getline(std::cin, input_line);

    if (input_line == "exit") {
      return; // 返回主菜单
    }

    // 检查输入
    std::stringstream ss(input_line);
    if (!(ss >> target_id) || !(ss.eof())) {
      std::cerr << "错误：输入无效，请输入一个数字学号。" << std::endl;
      continue;
    }

    // 检查范围
    if (target_id < 1 || target_id > NUM_RECORDS) {
      std::cerr << "错误：学号 " << target_id << " 超出范围1 - " << NUM_RECORDS << "。" << std::endl;
      continue;
    }

    Timer query_timer;

    // 计算偏移量
    long long offset = (long long)(target_id - 1) * sizeof(PackedStudentRecord);

    // 打开文件
    std::ifstream dat1_file(DAT1_FILE, std::ios::binary);
    if (!dat1_file.is_open()) {
      std::cerr << "错误：无法打开文件 " << DAT1_FILE << std::endl;
      return;
    }

    // 随机访问
    dat1_file.seekg(offset);

    // 读取单条记录
    PackedStudentRecord record;
    dat1_file.read(reinterpret_cast<char*>(&record), sizeof(PackedStudentRecord));

    dat1_file.close();

    double query_time_ms = query_timer.interval();

    if (record.id != target_id) {
      std::cerr << "错误：文件数据损坏。期望学号 " << target_id << "，但读取到 " << record.id << std::endl;
      continue;
    }

    // 解包并打印结果
    std::cout << std::fixed << std::setprecision(1); 
    std::cout << "  学号: " << record.id << std::endl;
    std::cout << "  语文: " << (record.chinese_x10 / 10.0f) << std::endl;
    std::cout << "  数学: " << (record.math_x10 / 10.0f) << std::endl;
    std::cout << "  英语: " << (record.english_x10 / 10.0f) << std::endl;
    std::cout << "  综合: " << (record.composite_x10 / 10.0f) << std::endl;
    std::cout << std::fixed << std::setprecision(6); 
    std::cout << "  查询耗时: " << query_time_ms << " 毫秒" << std::endl;
    std::cout << "---------------------------------------------" << std::endl;
  }
}

/* 任务1.6：根据语文成绩查询dat2和idx */
// 在索引中查找整数分数的byte偏移量
uint32_t findByteOffsetForScore(int score, const std::vector<uint8_t>& internal_nodes,
                                    const std::vector<uint32_t>& leaf_nodes)
{
  if (score < 0 || score >= NUM_LEAVES) {
    return UINT32_MAX;
  }

  int node_index = 0;
  
  // 遍历内部节点
  while (node_index < NUM_INTERNAL_NODES) {
    uint8_t split_value = internal_nodes[node_index];
    if (score <= split_value) {
      node_index = 2 * node_index + 1; // 往左
    } else {
      node_index = 2 * node_index + 2; // 往右
    }
  }

  int leaf_index = node_index - NUM_INTERNAL_NODES;

  if (leaf_index >= 0 && leaf_index < NUM_LEAVES) {
    return leaf_nodes[leaf_index];
  }
  
  return UINT32_MAX;
}

void queryByChineseScore(const std::vector<uint8_t>& internal_nodes,
                       const std::vector<uint32_t>& leaf_nodes)
{
  std::string input_line;

  std::cout << "\n[任务1.6：按语文成绩范围查询]" << std::endl;
  
  while (true) {
    std::cout << "请输入语文成绩范围，例如 89.5 92.1。输入exit回到主菜单: ";
    std::getline(std::cin, input_line);

    if (input_line == "exit") {
      return; // 返回主菜单
    }

    // 检查输入
    std::stringstream ss(input_line);
    float score_low_input, score_high_input;
    if (!(ss >> score_low_input >> score_high_input) || !(ss.eof())) {
      std::cerr << "错误：输入无效，请输入两个浮点数。例如: 89.5 92.1" << std::endl;
      continue;
    }

    float score_low = std::min(score_low_input, score_high_input);
    float score_high = std::max(score_low_input, score_high_input);

    // 调整范围
    if (score_high > 100.0f || score_low < 0.0f) {
      std::cerr << "错误：范围 " << score_low << "-" << score_high << "不在[0, 100]之内。" << std::endl;
      continue;
    }

    Timer query_timer;

    // 查找起始偏移量
    int int_score_to_find = static_cast<int>(score_high);
    uint32_t start_offset = UINT32_MAX;

    // 向下搜索，直到找到一个存在于索引中的分数
    while (start_offset == UINT32_MAX && int_score_to_find >= 0) {
      start_offset = findByteOffsetForScore(int_score_to_find, internal_nodes, leaf_nodes);
      int_score_to_find--;
    }

    int count = 0;
    double total_chinese_score = 0.0;

    if (start_offset != UINT32_MAX) {
      std::ifstream dat2_file(DAT2_FILE, std::ios::binary);
      if (!dat2_file.is_open()) {
        std::cerr << "错误：无法打开文件 " << DAT2_FILE << std::endl;
        return;
      }
      
      dat2_file.seekg(start_offset);
      PackedStudentRecord record;

      // 从起始位置开始读取
      while (dat2_file.read(reinterpret_cast<char*>(&record), sizeof(PackedStudentRecord))) {
        float chinese_score = record.chinese_x10 / 10.0f;
        if (chinese_score > score_high) {
          continue; 
        }
        if (chinese_score < score_low) {
          break;
        }
        count++;
        total_chinese_score += chinese_score;
      }
      dat2_file.close();
    }

    double query_time_ms = query_timer.interval();

    // 打印结果
    std::cout << std::fixed;
    std::cout << "  在 [" << std::setprecision(1) << score_low << ", " << score_high << "] 范围内查询完毕。" << std::endl;
    std::cout << "  查询到 " << count << " 名学生。" << std::endl;

    if (count > 0) {
      std::cout << "  平均语文成绩: " << std::setprecision(1) << (total_chinese_score / count) << std::endl;
    } else {
      std::cout << "  平均语文成绩: 无" << std::endl;
    }
    
    std::cout << "  查询耗时: " << std::setprecision(6) << query_time_ms << " 毫秒" << std::endl;
    std::cout << "---------------------------------------------" << std::endl;
  }
}

int main()
{
  // 在主循环前加载索引文件
  std::vector<uint8_t> internal_nodes(NUM_INTERNAL_NODES);
  std::vector<uint32_t> leaf_nodes(NUM_LEAVES);

  std::ifstream idx_file(IDX_FILE, std::ios::binary);
  if (!idx_file.is_open()) {
    std::cerr << "错误：无法加载索引文件 " << IDX_FILE << "。" << std::endl;
    std::cerr << "请先运行文件生成器。" << std::endl;
    return -1;
  }

  // 读取内部节点
  idx_file.read(reinterpret_cast<char*>(internal_nodes.data()), internal_nodes.size() * sizeof(uint8_t));
  if (idx_file.gcount() != internal_nodes.size() * sizeof(uint8_t)) {
      std::cerr << "错误：索引文件 " << IDX_FILE << " 已损坏或格式不正确。" << std::endl;
      return -1;
  }

  // 读取叶节点
  idx_file.read(reinterpret_cast<char*>(leaf_nodes.data()), leaf_nodes.size() * sizeof(uint32_t));
  if (idx_file.gcount() != leaf_nodes.size() * sizeof(uint32_t)) {
      std::cerr << "错误：索引文件 " << IDX_FILE << " 已损坏或格式不正确。" << std::endl;
      return -1;
  }
  
  idx_file.close();

  // 主菜单
  std::string choice;
  while (true) {
    std::cout << "--- 查询工具主菜单 ---" << std::endl;
    std::cout << "1. 按学号查询 (任务 1.5)" << std::endl;
    std::cout << "2. 按语文成绩查询 (任务 1.6)" << std::endl;
    std::cout << "输入 'exit' 退出程序" << std::endl;
    std::cout << "请选择: ";

    std::getline(std::cin, choice);

    if (choice == "1") {
      queryByID();
    } else if (choice == "2") {
      queryByChineseScore(internal_nodes, leaf_nodes);
    } else if (choice == "exit") {
      break;
    } else {
      std::cerr << "无效选项，请输入 1, 2, 或 exit。" << std::endl;
    }
  }
  return 0;
}