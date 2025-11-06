#include "hw2-common.h"
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

// 服务器IP
const char* SERVER_IP = "127.0.0.1";

// 连接到服务器并返回 socket
int connectToServer()
{
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    std::cerr << "错误：无法创建 socket" << std::endl;
    return -1;
  }

  sockaddr_in server_addr;
  std::memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  
  if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
    std::cerr << "错误：无效的服务器 IP 地址" << std::endl;
    close(sock);
    return -1;
  }

  if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
    std::cerr << "错误：无法连接到服务器 " << SERVER_IP << ":" << SERVER_PORT << "。" << std::endl;
    std::cerr << "  请确保服务器正在运行。" << std::endl;
    close(sock);
    return -1;
  }
  
  return sock;
}

/* 任务1.5：客户端查询 */
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

    // 准备请求
    QueryRequest req;
    req.type = 1;
    req.f_param1 = static_cast<float>(target_id);
    req.f_param2 = 0; // 未使用

    // 连接
    int sock = connectToServer();
    if (sock == -1) continue; // 连接失败，等待下一次输入

    // 发送和接收
    QueryResponse resp;
    
    int bytes_sent = send(sock, reinterpret_cast<char*>(&req), sizeof(req), 0);
    if (bytes_sent != sizeof(req)) {
      std::cerr << "错误：发送请求到服务器失败。" << std::endl;
      close(sock);
      continue;
    }

    int bytes_read = recv(sock, reinterpret_cast<char*>(&resp), sizeof(resp), 0);
    if (bytes_read != sizeof(resp)) {
      std::cerr << "错误：从服务器接收响应失败 (接收到 " << bytes_read << " 字节, 期望 " << sizeof(resp) << ")。" << std::endl;
      std::cerr << "  这可能意味着服务器线程崩溃了。" << std::endl;
      close(sock);
      continue;
    }
    
    close(sock);

    double query_time_ms = query_timer.interval();

    // 解包并打印结果
    if (resp.type == 11) { 
      std::cout << std::fixed << std::setprecision(1); 
      std::cout << "  学号: " << target_id << std::endl; // 服务器不回传 ID，我们直接用
      std::cout << "  语文: " << resp.data.scores[0] << std::endl;
      std::cout << "  数学: " << resp.data.scores[1] << std::endl;
      std::cout << "  英语: " << resp.data.scores[2] << std::endl;
      std::cout << "  综合: " << resp.data.scores[3] << std::endl;
    } else {
      std::cerr << "错误：服务器返回错误 (可能 ID 未找到或文件损坏)。" << std::endl;
    }
    
    std::cout << std::fixed << std::setprecision(6); 
    std::cout << "  查询耗时 (含网络): " << query_time_ms << " 毫秒" << std::endl;
    std::cout << "---------------------------------------------" << std::endl;
  }
}

/* 任务1.6：客户端查询 */
void queryByChineseScore()
{
  std::string input_line;

  std::cout << "\n[任务1.6：按语文成绩范围查询]" << std::endl;
  
  while (true) {
    std::cout << "请输入语文成绩范围 (0.0 - 100.0)，或输入exit回到主菜单: ";
    std::getline(std::cin, input_line);

    if (input_line == "exit") {
      return; // 返回主菜单
    }

    std::stringstream ss(input_line);
    float score_low_input, score_high_input;
    if (!(ss >> score_low_input >> score_high_input) || !(ss.eof())) {
      std::cerr << "错误：输入无效，请输入两个浮点数 (例如: 89.5 92.1)。" << std::endl;
      continue;
    }

    if (score_low_input < 0.0f || score_low_input > 100.0f ||
        score_high_input < 0.0f || score_high_input > 100.0f) {
      std::cerr << "错误：分数必须在 0.0 到 100.0 之间。" << std::endl;
      continue;
    }

    float score_low = std::min(score_low_input, score_high_input);
    float score_high = std::max(score_low_input, score_high_input);
    
    Timer query_timer;
    
    // 准备请求
    QueryRequest req;
    req.type = 2;
    req.f_param1 = score_low;
    req.f_param2 = score_high;

    // 连接
    int sock = connectToServer();
    if (sock == -1) continue;

    // 发送和接收
    QueryResponse resp;

    int bytes_sent = send(sock, reinterpret_cast<char*>(&req), sizeof(req), 0);
    if (bytes_sent != sizeof(req)) {
      std::cerr << "错误：发送请求到服务器失败。" << std::endl;
      close(sock);
      continue;
    }

    int bytes_read = recv(sock, reinterpret_cast<char*>(&resp), sizeof(resp), 0);
    if (bytes_read != sizeof(resp)) {
      std::cerr << "错误：从服务器接收响应失败 (接收到 " << bytes_read << " 字节, 期望 " << sizeof(resp) << ")。" << std::endl;
      std::cerr << "  这可能意味着服务器线程崩溃了。" << std::endl;
      close(sock);
      continue;
    }
    
    close(sock);

    double query_time_ms = query_timer.interval();

    // 解包并打印结果
    if (resp.type == 12) {
      std::cout << std::fixed; 
      std::cout << "  在 [" << std::setprecision(1) << score_low << ", " << score_high << "] 范围内查询完毕。" << std::endl;
      std::cout << "  查询到 " << resp.data.score_result.count << " 名学生。" << std::endl;

      if (resp.data.score_result.count > 0) {
        std::cout << "  平均语文成绩: " << std::setprecision(1) << resp.data.score_result.average << std::endl;
      } else {
        std::cout << "  平均语文成绩: N/A" << std::endl;
      }
    } else {
      std::cerr << "错误：服务器返回错误。" << std::endl;
    }
    
    std::cout << "  查询耗时 (含网络): " << std::setprecision(6) << query_time_ms << " 毫秒" << std::endl;
    std::cout << "---------------------------------------------" << std::endl;
  }
}

int main()
{
  std::cout << "客户端启动成功，准备连接到 " << SERVER_IP << ":" << SERVER_PORT << std::endl;

  std::string choice;
  while (true) {
    std::cout << "\n--- 查询工具主菜单 (网络版) ---" << std::endl;
    std::cout << "1. 按学号查询 (任务 1.5)" << std::endl;
    std::cout << "2. 按语文成绩查询 (任务 1.6)" << std::endl;
    std::cout << "输入 'exit' 退出程序" << std::endl;
    std::cout << "请选择: ";

    std::getline(std::cin, choice);

    if (choice == "1") {
      queryByID();
    } else if (choice == "2") {
      queryByChineseScore();
    } else if (choice == "exit") {
      break;
    } else {
      std::cerr << "无效选项，请输入 1, 2, 或 exit。" << std::endl;
    }
  }
  return 0;
}