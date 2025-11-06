#include "hw2-common.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

// --- 全局变量 ---
std::vector<uint8_t> g_internal_nodes;
std::vector<uint32_t> g_leaf_nodes;
std::mutex g_dat_file_mutex; 
std::mutex g_log_mutex;

// 在索引中查找整数分数的byte偏移量
uint32_t findByteOffsetForScore(int score)
{
  if (score < 0 || score >= NUM_LEAVES) {
    return UINT32_MAX;
  }
  int node_index = 0; 
  while (node_index < NUM_INTERNAL_NODES) {
    uint8_t split_value = g_internal_nodes[node_index];
    if (score <= split_value) {
      node_index = 2 * node_index + 1;
    } else {
      node_index = 2 * node_index + 2;
    }
  }
  int leaf_index = node_index - NUM_INTERNAL_NODES;
  if (leaf_index >= 0 && leaf_index < NUM_LEAVES) {
    return g_leaf_nodes[leaf_index];
  }
  return UINT32_MAX;
}

/* 任务1.5：服务器端的核心逻辑 */
QueryResponse queryByIDinServer(const QueryRequest& req)
{
  QueryResponse resp;
  int target_id = static_cast<int>(req.f_param1);

  if (target_id < 1 || target_id > NUM_RECORDS) {
    resp.type = 255; // 错误：范围
    return resp;
  }

  long long offset = (long long)(target_id - 1) * sizeof(PackedStudentRecord);

  std::lock_guard<std::mutex> lock(g_dat_file_mutex);

  std::ifstream dat1_file(DAT1_FILE, std::ios::binary);
  if (!dat1_file.is_open()) {
    resp.type = 255; // 错误：无法打开文件
    return resp;
  }

  dat1_file.seekg(offset);
  PackedStudentRecord record;
  dat1_file.read(reinterpret_cast<char*>(&record), sizeof(PackedStudentRecord));
  dat1_file.close();

  if (record.id != target_id) {
    resp.type = 255; // 错误：数据损坏
    return resp;
  }

  // 成功
  resp.type = 11;
  resp.data.scores[0] = record.chinese_x10 / 10.0f;
  resp.data.scores[1] = record.math_x10 / 10.0f;
  resp.data.scores[2] = record.english_x10 / 10.0f;
  resp.data.scores[3] = record.composite_x10 / 10.0f;
  
  return resp;
}

/* 任务1.6：服务器端的核心逻辑 */
QueryResponse queryByChineseScoreInServer(const QueryRequest& req)
{
  QueryResponse resp;
  
  float score_low = std::min(req.f_param1, req.f_param2);
  float score_high = std::max(req.f_param1, req.f_param2);
  score_low = std::max(0.0f, score_low);
  score_high = std::min(100.0f, score_high);

  int int_score_to_find = static_cast<int>(score_high);
  uint32_t start_offset = UINT32_MAX;

  while (start_offset == UINT32_MAX && int_score_to_find >= 0) {
    start_offset = findByteOffsetForScore(int_score_to_find);
    int_score_to_find--;
  }

  int count = 0;
  double total_chinese_score = 0.0;

  if (start_offset != UINT32_MAX) {
    std::lock_guard<std::mutex> lock(g_dat_file_mutex);
    
    std::ifstream dat2_file(DAT2_FILE, std::ios::binary);
    if (dat2_file.is_open()) {
      dat2_file.seekg(start_offset);
      PackedStudentRecord record;
      
      while (dat2_file.read(reinterpret_cast<char*>(&record), sizeof(PackedStudentRecord))) {
        float chinese_score = record.chinese_x10 / 10.0f;
        if (chinese_score > score_high) continue;
        if (chinese_score < score_low) break;
        count++;
        total_chinese_score += chinese_score;
      }
      dat2_file.close();
    }
  }

  // 成功
  resp.type = 12;
  resp.data.score_result.count = count;
  resp.data.score_result.average = (count > 0) ? (total_chinese_score / count) : 0.0;
  
  return resp;
}

// 处理单个客户端连接
void handleClient(int client_sock)
{
  // 获取线程ID，用于日志
  std::thread::id this_id = std::this_thread::get_id();
  
  { // 加锁打印
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << "[线程 " << this_id << "] 启动，等待接收 " << sizeof(QueryRequest) << " 字节..." << std::endl;
  }

  QueryRequest req;
  QueryResponse resp;

  // 接收请求
  int bytes_read = recv(client_sock, reinterpret_cast<char*>(&req), sizeof(req), 0);
  
  {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << "[线程 " << this_id << "] 接收到 " << bytes_read << " 字节。" << std::endl;
  }

  if (bytes_read == sizeof(req)) {
    {
      std::lock_guard<std::mutex> lock(g_log_mutex);
      std::cout << "[线程 " << this_id << "] 请求类型: " << (int)req.type << "，参数1: " << req.f_param1 << std::endl;
    }

    // 根据请求类型处理
    if (req.type == 1) {
      resp = queryByIDinServer(req);
    } else if (req.type == 2) {
      resp = queryByChineseScoreInServer(req);
    } else {
      resp.type = 255; // 错误：未知请求类型
    }
  } else {
    resp.type = 255; // 错误：接收数据不完整
  }

  {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << "[线程 " << this_id << "] 处理完毕，响应类型: " << (int)resp.type << "。准备发送 " << sizeof(resp) << " 字节..." << std::endl;
  }

  // 发送响应
  int bytes_sent = send(client_sock, reinterpret_cast<char*>(&resp), sizeof(resp), 0);
  
  {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << "[线程 " << this_id << "] 发送了 " << bytes_sent << " 字节。" << std::endl;
  }

  // 关闭连接
  close(client_sock);
  {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << "[线程 " << this_id << "] 关闭连接，线程退出。" << std::endl;
  }
}

// 加载索引文件到全局变量
bool loadIndexFile()
{
  g_internal_nodes.resize(NUM_INTERNAL_NODES);
  g_leaf_nodes.resize(NUM_LEAVES);

  std::ifstream idx_file(IDX_FILE, std::ios::binary);
  if (!idx_file.is_open()) {
    std::cerr << "错误：无法加载索引文件 " << IDX_FILE << "。" << std::endl;
    return false;
  }
  
  idx_file.read(reinterpret_cast<char*>(g_internal_nodes.data()), g_internal_nodes.size() * sizeof(uint8_t));
  idx_file.read(reinterpret_cast<char*>(g_leaf_nodes.data()), g_leaf_nodes.size() * sizeof(uint32_t));
  idx_file.close();
  
  if (idx_file.gcount() != (g_leaf_nodes.size() * sizeof(uint32_t))) {
      std::cerr << "错误：索引文件 " << IDX_FILE << " 已损坏或格式不正确。" << std::endl;
      return false;
  }
  
  std::cout << "索引文件 " << IDX_FILE << " (504 字节) 加载成功。" << std::endl;
  return true;
}

int main()
{
  // 加载索引
  if (!loadIndexFile()) {
    std::cerr << "请先运行文件生成器。" << std::endl;
    return -1;
  }

  // 设置网络
  int server_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (server_sock == -1) {
    std::cerr << "错误：无法创建 socket" << std::endl;
    return -1;
  }

  sockaddr_in server_addr;
  std::memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(SERVER_PORT);

  // 允许端口重用
  int reuse = 1;
  setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
    std::cerr << "错误：无法 bind 端口 " << SERVER_PORT << std::endl;
    return -1;
  }

  if (listen(server_sock, 10) == -1) {
    std::cerr << "错误：无法 listen" << std::endl;
    return -1;
  }

  std::cout << "服务器启动成功，正在 " << SERVER_PORT << " 端口上监听..." << std::endl;

  // 接受-分离循环
  while (1) {
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
    
    if (client_sock == -1) {
      std::cerr << "警告：accept 失败" << std::endl;
      continue;
    }
    
    {
      std::lock_guard<std::mutex> lock(g_log_mutex);
      std::cout << "[主线程] 接受来自 " << inet_ntoa(client_addr.sin_addr) << " 的连接，创建新线程..." << std::endl;
    }

    // 任务2.1：单线程处理
    // handleClient(client_sock);

    // 任务2.2：多线程处理
    std::thread(handleClient, client_sock).detach();
  }

  close(server_sock);
  return 0;
}