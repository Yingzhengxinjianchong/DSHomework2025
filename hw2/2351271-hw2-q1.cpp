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
#include <future>

// 学号
const std::string STUDENT_ID = "2351271";

// 文件名
const std::string TXT_FILE = STUDENT_ID + "-hw2.txt";
const std::string DAT1_FILE = STUDENT_ID + "-hw2.dat1";
const std::string DAT2_FILE = STUDENT_ID + "-hw2.dat2";
const std::string IDX_FILE = STUDENT_ID + "-hw2.idx";

// 记录总数
const int NUM_RECORDS = 512 * 32 * 8;

// 并行排序的阈值
const size_t PARALLEL_SORT_THRESHOLD = 4096; 

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

/* 并行生成所有数据 */
struct WorkBatch {
  int batch_id;
  int start_id;
  int num_records;
};

// 生成任务列表
std::queue<WorkBatch> gen_work_queue;
std::mutex gen_work_mutex;
std::condition_variable gen_work_cv;
bool gen_work_done = false;

// 生成结果列表
std::map<int, std::vector<StudentRecord>> gen_finished_work;
std::mutex gen_finished_mutex;
std::condition_variable gen_finished_cv;

// 工人线程，只生成数据
void generationWorkerThread()
{
  // 初始化高斯分布
  std::random_device rd;
  std::mt19937 gen(rd());
  std::normal_distribution<float> dist(70.0f, 20.0f);

  while(1){
    struct WorkBatch batch_to_process;

    // 领取任务
    {
      std::unique_lock<std::mutex> lock(gen_work_mutex);
      gen_work_cv.wait(lock, []{ return !gen_work_queue.empty()|| gen_work_done;});

      if (gen_work_queue.empty() && gen_work_done) {
        return;
      }

      batch_to_process = gen_work_queue.front();
      gen_work_queue.pop();
    }

    // 内存缓冲区
    std::vector<StudentRecord> batch_records;
    batch_records.reserve(batch_to_process.num_records);
    int end_id = batch_to_process.start_id + batch_to_process.num_records;

    for(int id = batch_to_process.start_id; id < end_id; ++id){
      // 生成
      StudentRecord record;
      record.id = id;
      record.chinese = generateGaussianScore(gen, dist);
      record.math = generateGaussianScore(gen, dist);
      record.english = generateGaussianScore(gen, dist);
      record.composite = generateGaussianScore(gen, dist);

      // 存入
      batch_records.push_back(record);
    }

    // 写入已完成map
    {
      std::unique_lock<std::mutex> lock(gen_finished_mutex);
      gen_finished_work[batch_to_process.batch_id] = std::move(batch_records);
    }

    // 唤醒写入线程
    gen_finished_cv.notify_one();
  }
};

/* 生成数据 */
std::vector<StudentRecord> generateAllRecordsInMemory(const int num_batches)
{
  // 重置全局变量
  gen_work_done = false;
  gen_work_queue = {};
  gen_finished_work = {};

  // 线程数与批次大小
  const int num_threads = std::thread::hardware_concurrency();
  const int records_per_batch = (NUM_RECORDS + num_batches - 1) / num_batches;

  // 创建并分发待办任务
  {
    std::unique_lock<std::mutex> lock(gen_work_mutex);
    for (int i = 0; i < num_batches; ++i) {
      int start_id = i * records_per_batch + 1;
      int num_records = std::min(records_per_batch, NUM_RECORDS - (start_id - 1));
      if (num_records <= 0) break;

      gen_work_queue.push({i, start_id, num_records});
    }
  }

  // 启动工人线程
  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; ++i){
    threads.emplace_back(generationWorkerThread);
  }

  // 主线程按顺序收集数据
  std::vector<StudentRecord> all_records;
  all_records.reserve(NUM_RECORDS);
  int next_batch_to_collect = 0;

  // 写入工作
  while (next_batch_to_collect < num_batches) {
    std::vector<StudentRecord> batch_data;
    bool found_batch = false;

    // 检查是否有下一批
    {
      std::unique_lock<std::mutex> lock(gen_finished_mutex);
      gen_finished_cv.wait(lock, [&] {
        return gen_finished_work.find(next_batch_to_collect) != gen_finished_work.end();
      });
      
      batch_data = std::move(gen_finished_work.at(next_batch_to_collect));
      gen_finished_work.erase(next_batch_to_collect);
      found_batch = true;
    }

    // 有，存入主vector
    if (found_batch) {
      all_records.insert(
          all_records.end(),
          std::make_move_iterator(batch_data.begin()),
          std::make_move_iterator(batch_data.end())
      );
      next_batch_to_collect++;
    }

  }

  // 收尾
  {
    std::unique_lock<std::mutex> lock(gen_work_mutex);
    gen_work_done = true;
  }
  gen_work_cv.notify_all();

  for (std::thread& t : threads) {
    t.join();
  }

  return all_records;
};


/* 任务1.1：并行生成txt */
struct TxtWorkBatch {
    int batch_id;
    size_t start_index; // all_records 中的索引
    int num_records;
};

// TXT转换任务列表
std::queue<TxtWorkBatch> txt_work_queue;
std::mutex txt_work_mutex;
std::condition_variable txt_work_cv;
bool txt_work_done = false;

// TXT转换结果列表
std::map<int, std::string> txt_finished_work;
std::mutex txt_finished_mutex;
std::condition_variable txt_finished_cv;


// TXT转换工人线程
void txtConversionWorkerThread(const std::vector<StudentRecord>* all_records)
{
  while(1) {
    TxtWorkBatch batch_to_process;

    // 领取任务
    {
      std::unique_lock<std::mutex> lock(txt_work_mutex);
      txt_work_cv.wait(lock, [] { return !txt_work_queue.empty() || txt_work_done; });

      if (txt_work_queue.empty() && txt_work_done) {
        return;
      }

      batch_to_process = txt_work_queue.front();
      txt_work_queue.pop();
    }

    // 缓冲区
    std::stringstream buffer_ss;
    size_t end_index = batch_to_process.start_index + batch_to_process.num_records;

    // 执行字符串转换
    for (size_t i = batch_to_process.start_index; i < end_index; ++i) {
      const StudentRecord& record = (*all_records)[i];
           
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

    // 提交结果
    {
      std::unique_lock<std::mutex> lock(txt_finished_mutex);
      txt_finished_work[batch_to_process.batch_id] = buffer_ss.str();
    }
    txt_finished_cv.notify_one();
  }
}

void generateTXT(const std::vector<StudentRecord>& all_records)
{
  // 创建文件流
  std::ofstream txt_file(TXT_FILE);
  if (!txt_file.is_open()) {
    std::cerr << "错误：无法创建文件" << TXT_FILE << std::endl;
    return;
  }

  // 重置全局变量
  txt_work_done = false;
  txt_work_queue = {};
  txt_finished_work = {};

  const int num_batches = 16; // 同样分为16批
  const int num_threads = std::thread::hardware_concurrency();
  const int records_per_batch = (NUM_RECORDS + num_batches - 1) / num_batches;

  // 分发任务
  {
    std::unique_lock<std::mutex> lock(txt_work_mutex);
    for (int i = 0; i < num_batches; ++i) {
      size_t start_index = static_cast<size_t>(i) * records_per_batch;
      if (start_index >= all_records.size()) break;

      int num_records = std::min(records_per_batch, 
                             static_cast<int>(all_records.size() - start_index));

      txt_work_queue.push({i, start_index, num_records});
    }
  }

  // 启动工人线程
  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; ++i) {
    // 传递 all_records 的指针
    threads.emplace_back(txtConversionWorkerThread, &all_records);
  }

  // 主线程按顺序收集字符串并写入文件
  int next_batch_to_collect = 0;
  while (next_batch_to_collect < num_batches) {
    std::string batch_data;
    bool found_batch = false;

    // 检查是否有下一批
    {
      std::unique_lock<std::mutex> lock(txt_finished_mutex);
      txt_finished_cv.wait(lock, [&] {
        return txt_finished_work.find(next_batch_to_collect) != txt_finished_work.end();
      });
          
      batch_data = std::move(txt_finished_work.at(next_batch_to_collect));
      txt_finished_work.erase(next_batch_to_collect);
      found_batch = true;
    }

    // 有，写入文件
    if (found_batch) {
      txt_file << batch_data;
      next_batch_to_collect++;
    }
  }

  // 收尾
  {
    std::unique_lock<std::mutex> lock(txt_work_mutex);
    txt_work_done = true;
  }
  txt_work_cv.notify_all();

  for (std::thread& t : threads) {
    t.join();
  }

  txt_file.close();
}


/* 任务1.2：生成dat1*/
void generateDAT1(const std::vector<StudentRecord>& all_records)
{
  std::ofstream dat1_file(DAT1_FILE, std::ios::binary);
  if (!dat1_file.is_open()) {
    std::cerr << "错误：无法创建文件" << DAT1_FILE << std::endl;
    return;
  }

  // 一次性写入
  dat1_file.write(
    reinterpret_cast<const char*>(all_records.data()),
    all_records.size() * sizeof(StudentRecord)
  );

  dat1_file.close();
}

/* 任务1.3：生成dat2 */
// 比较器
template<typename Compare>
void merge (std::vector<StudentRecord>& vec, std::vector<StudentRecord>& temp_vec,
            size_t left, size_t mid, size_t right, Compare comp)
{
  size_t i = left;
  size_t j = mid + 1;
  size_t k = left;

  while (i <= mid && j <= right) {
    if (comp(vec[i], vec[j])) {
      temp_vec[k++] = vec[i++];
    }
    else {
      temp_vec[k++] = vec[j++];
    }
  }

  while (i <= mid) {
    temp_vec[k++] = vec[i++];
  }

  while (j <= right) {
    temp_vec[k++] = vec[j++];
  }

  for (i = left; i <= right; ++i){
    vec[i] = temp_vec[i];
  }
};

// 归并排序
template<typename Compare>
void mergeSortRecursive (std::vector<StudentRecord>& vec, std::vector<StudentRecord>& temp_vec,
                        size_t left, size_t right, Compare comp)
{
  if (left >= right) {
    return;
  }

  size_t mid = (left + right) / 2;

  if (right - left < PARALLEL_SORT_THRESHOLD) {
    // 数据块小，单线程
    mergeSortRecursive(vec, temp_vec, left, mid, comp);
    mergeSortRecursive(vec, temp_vec, mid + 1, right, comp);
  } 
  else {
    // 数据块大，多线程
    auto future_left = std::async(std::launch::async, &mergeSortRecursive<Compare>,
                                  std::ref(vec), std::ref(temp_vec), left, mid, comp);
    // 当前线程同步处理右半部分
    mergeSortRecursive(vec, temp_vec, mid + 1, right, comp);
    future_left.get();
    }

    merge(vec, temp_vec, left, mid, right, comp);
};

void generateDAT2(const std::vector<StudentRecord>& all_records)
{
  std::ofstream dat2_file(DAT2_FILE, std::ios::binary);
  if (!dat2_file.is_open()) {
    std::cerr << "错误：无法创建文件" << DAT2_FILE << std::endl;
    return;
  }

  std::vector<StudentRecord> sorted_records = all_records;
  std::vector<StudentRecord> temp_vec(sorted_records.size());

  auto compareStudents = [](const StudentRecord& a, const StudentRecord& b){
    if (a.chinese != b.chinese) {
      return a.chinese > b.chinese;
    }
    return a.id < b.id;
  };

  mergeSortRecursive(sorted_records, temp_vec, 0, sorted_records.size()-1, compareStudents);

  dat2_file.write(
    reinterpret_cast<const char*>(sorted_records.data()),
    sorted_records.size() * sizeof(StudentRecord)
  );

  dat2_file.close();
}

int main()
{
  // 数据生成
  Timer gen_timer;
  std::vector<StudentRecord> all_records = generateAllRecordsInMemory(16);
  double gen_time_ms = gen_timer.interval();

  // 任务1.1
  Timer timer_1_1;
  generateTXT(all_records);
  double write_1_1_ms = timer_1_1.interval();
  std::cout << TXT_FILE << "生成完毕，生成数据+写入文件耗时：" << gen_time_ms << " + "
            << write_1_1_ms << " = " << (gen_time_ms + write_1_1_ms) << "毫秒" <<
            std::endl;
  std::cout << "---------------------------------------------" << std::endl;

  // 任务1.2
  Timer timer_1_2;
  generateDAT1(all_records);
  double write_1_2_ms = timer_1_2.interval();
  std::cout << DAT1_FILE << "生成完毕，生成数据+写入文件耗时：" << gen_time_ms << " + "
            << write_1_2_ms << " = " << (gen_time_ms + write_1_2_ms) << "毫秒" <<
            std::endl;
  std::cout << "---------------------------------------------" << std::endl;

  // 任务1.3
  Timer timer_1_3;
  generateDAT2(all_records);
  double write_1_3_ms = timer_1_3.interval();
  std::cout << DAT2_FILE << "生成完毕，生成数据+写入文件耗时：" << gen_time_ms << " + "
            << write_1_3_ms << " = " << (gen_time_ms + write_1_3_ms) << "毫秒" <<
            std::endl;
  std::cout << "---------------------------------------------" << std::endl;

  return 0;
}