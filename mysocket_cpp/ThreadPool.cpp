#include "ThreadPool.h"

ThreadPool::ThreadPool(int numThreads) : stop_(false)
{
    for (int i = 0; i < numThreads; ++i)
    {
        threads_.emplace_back([this]
                              {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mtx_);
                    condition_.wait(lock, [this]
                    {
                        return !tasks_.empty() || stop_;
                    });
                    if (stop_ && tasks_.empty())
                        return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            } });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(mtx_);
        stop_ = true;
    }
    condition_.notify_all();
    for (auto &t : threads_)
        t.join();
}