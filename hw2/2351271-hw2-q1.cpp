#include <iostream>
#include <random>
#include <cmath>
#include <algorithm>
#include <map>
#include <chrono>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <memory>
#include <condition_variable>

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

/* 任务1.1：生成txt */
// 快速写入器
inline void fast_write_float(std::stringstream& ss, float score)
{
  int fixed_score = static_cast<int>(std::round(score * 10.0f));
  ss << std::abs(fixed_score / 10) << '.' << std::abs(fixed_score % 10);
};

// 分数生成器
float generateGaussianScore(std::mt19937& gen, std::normal_distribution<float>& dist)
{
  float score;
  while(1){
    score = dist(gen);
    if(score >= 0.0f && score <= 100.0f){
      break;
    }
  }
  return score;
};

// 多线程
struct WorkBatch {
  int batch_id;
  int start_id;
  int num_records;
};

struct ResultBatch {
  int batch_id;
  std::string data;
};

// 待办任务
std::queue<WorkBatch> work_queue;
std::mutex work_mutex;
std::condition_variable work_cv;
bool work_done = false;

// 结果
std::map<int, std::string> finished_work;
std::mutex finished_mutex;
std::condition_variable finished_cv;

// 执行工人线程
void worker_thread()
{
  // 初始化高斯分布
  std::random_device rd;
  std::mt19937 gen(rd());
  std::normal_distribution<float> dist(70.0f, 20.0f);

  while(1){
    struct WorkBatch batch_to_process;

    // 领取任务
    {
      std::unique_lock<std::mutex> lock(work_mutex);
      work_cv.wait(lock, []{ return !work_queue.empty()|| work_done;});

      if (work_queue.empty() && work_done) {
        return;
      }

      batch_to_process = work_queue.front();
      work_queue.pop();
    }

    // 内存缓冲区
    std::stringstream buffer_ss;
    int end_id = batch_to_process.start_id + batch_to_process.num_records;

    // 生成和写入
    for(int id = batch_to_process.start_id; id < end_id; ++id){
      // 生成
      class StudentRecord record;
      record.id = id;
      record.chinese = generateGaussianScore(gen, dist);
      record.math = generateGaussianScore(gen, dist);
      record.english = generateGaussianScore(gen, dist);
      record.composite = generateGaussianScore(gen, dist);
      
      // 写入缓冲区
      buffer_ss << record.id << ",";
      fast_write_float(buffer_ss, record.chinese);
      buffer_ss << ",";
      fast_write_float(buffer_ss, record.math);
      buffer_ss << ",";
      fast_write_float(buffer_ss, record.english);
      buffer_ss << ",";
      fast_write_float(buffer_ss, record.composite);
      buffer_ss << "\n";
    }

    // 写入已完成map
    {
      std::unique_lock<std::mutex> lock(finished_mutex);
      finished_work[batch_to_process.batch_id] = buffer_ss.str();
    }

    // 唤醒写入线程
    finished_cv.notify_one();
  }
};

// 创建记录并写入txt文件
void generateRecords(const int num_batches)
{
  // 计时器
  class Timer timer;

  // 线程数与批次大小
  const int num_threads = std::thread::hardware_concurrency();
  const int records_per_batch = (NUM_RECORDS + num_batches - 1) / num_batches;

  // 创建并分发待办任务
  {
    std::unique_lock<std::mutex> lock(work_mutex);
    for (int i = 0; i < num_batches; ++i) {
      int start_id = i * records_per_batch + 1;
      int num_records = std::min(records_per_batch, NUM_RECORDS - (start_id - 1));
      if (num_records <= 0) break;

      work_queue.push({i, start_id, num_records});
    }
  }

  // 启动工人线程
  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; ++i){
    threads.emplace_back(worker_thread);
  }


  // 创建文件流
  std::ofstream txt_file(TXT_FILE);
  if(!txt_file.is_open()){
    std::cerr << "错误：无法创建文件" << TXT_FILE << std::endl;
    return;
  }
  int next_batch_to_write = 0;

  // 写入工作
  while (next_batch_to_write < num_batches) {
    std::string batch_data;
    bool found_batch = false;

    // 检查是否有下一批
    {
      std::unique_lock<std::mutex> lock(finished_mutex);
      finished_cv.wait(lock, [&] {
        return finished_work.find(next_batch_to_write) != finished_work.end();
      });
      
      batch_data = std::move(finished_work.at(next_batch_to_write));
      finished_work.erase(next_batch_to_write);
      found_batch = true;
    }

    // 有，写入文件
    if (found_batch) {
      txt_file << batch_data;
      next_batch_to_write++;
    }

  }

  // 收尾
  {
    std::unique_lock<std::mutex> lock(work_mutex);
    work_done = true;
  }
  work_cv.notify_all();

  for (std::thread& t : threads) {
    t.join();
  }

  // 关闭文件
  txt_file.close();

  // 停止计时
  std::cout << TXT_FILE << "生成完毕，耗时：" << timer.interval() << "毫秒" << std::endl;
};

int main()
{
  // 任务1.1
    generateRecords(16);

  return 0;
}