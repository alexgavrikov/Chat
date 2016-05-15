/*
 * thread_pool.h
 *
 *  Created on: 5 апр. 2016 г.
 *      Author: user
 */

#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

/*
 * Flexible threads pool with opportunity to add new pools if
 * there are no available at the moment
 */

#include <vector>
#include <list>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
  ThreadPool(size_t);
  void ForWorker();
  template<class F, class ... Args>
  auto enqueue(F&& f, Args&&... args)
  -> std::future<typename std::result_of<F(Args...)>::type>;bool AllFree() const;
  size_t UnfinishedTasksCount();
  ~ThreadPool();
private:
  // need to keep track of threads so we can join them
  std::list<std::thread> workers;
  // the task queue
  std::queue<std::function<void()> > tasks;

  // synchronization
  std::mutex queue_mutex;
  std::condition_variable condition;bool stop;
  std::atomic<size_t> free_workers_count;
};

void ThreadPool::ForWorker() {
//  ++free_workers_count;
  for (;;) {
    std::function<void()> task;

    {
      std::unique_lock<std::mutex> lock(this->queue_mutex);
      this->condition.wait(lock,
          [this] {return this->stop || !this->tasks.empty();});
      if (this->stop && this->tasks.empty())
        return;
      --free_workers_count;
      task = std::move(this->tasks.front());
      this->tasks.pop();
    }

    task();
    ++free_workers_count;
  }
}

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t initial_threads_count)
    : stop(false), free_workers_count(initial_threads_count) {
  for (size_t i = 0; i < initial_threads_count; ++i)
    workers.emplace_back([this]() {ForWorker();});
}

// add new work item to the pool
template<class F, class ... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
-> std::future<typename std::result_of<F(Args...)>::type> {
  using return_type = typename std::result_of<F(Args...)>::type;

  auto task = std::make_shared<std::packaged_task<return_type()> >(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  std::future<return_type> res = task->get_future();
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    // don't allow enqueueing after stopping the pool
    if (stop)
      throw std::runtime_error("enqueue on stopped ThreadPool");

    tasks.emplace([task]() {(*task)();});
    if (free_workers_count.load() < tasks.size()) {
      ++free_workers_count;
      workers.emplace_back([this]() {
        ForWorker();
      });
    }
  }
  condition.notify_one();
  return res;
}

bool ThreadPool::AllFree() const {
  return tasks.empty() && free_workers_count == workers.size();
}

size_t ThreadPool::UnfinishedTasksCount() {
  std::unique_lock<std::mutex> lock(this->queue_mutex);
  return tasks.size() + workers.size() - free_workers_count;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    stop = true;
  }
  condition.notify_all();
  for (std::thread &worker : workers)
    worker.join();
}

#endif /* THREAD_POOL_H_ */
