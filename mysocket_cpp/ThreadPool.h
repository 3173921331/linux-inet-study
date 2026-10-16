#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <tuple>

class ThreadPool
{
public:
    explicit ThreadPool(int numThreads);
    ~ThreadPool();

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    // 模板必须放头文件
    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ResultType = std::invoke_result_t<F, Args...>;

        auto task_ptr = std::make_shared<std::packaged_task<ResultType()>>(
            [f = std::forward<F>(f),
             args_tuple = std::make_tuple(std::forward<Args>(args)...)]() mutable -> ResultType
            {
                return std::apply(
                    [&f](auto &&...args)
                    {
                        return std::invoke(std::move(f), std::forward<decltype(args)>(args)...);
                    },
                    std::move(args_tuple));
            });

        auto future_result = task_ptr->get_future();

        {
            std::unique_lock<std::mutex> lock(mtx_);
            tasks_.emplace([t = std::move(task_ptr)]()
                           { (*t)(); });
        }
        condition_.notify_one();
        return future_result;
    }

private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable condition_;
    bool stop_;
};