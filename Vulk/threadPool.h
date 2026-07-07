#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <type_traits>
#include <memory>
#include <stdexcept>
#include <atomic> // 🚀 Added for tracking active tasks safely

class ThreadPool {
public:
    ThreadPool(size_t threads) : stop(false), active_tasks(0) { // 🚀 Initialize counter
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                            });

                        if (this->stop && this->tasks.empty()) {
                            return;
                        }

                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    task(); // Execute the chunk generation job

                    // 🚀 Task complete: Decrement and notify WaitForAll if we hit zero
                    {
                        std::lock_guard<std::mutex> lock(this->queue_mutex);
                        if (--active_tasks == 0) {
                            this->wait_condition.notify_all();
                        }
                    }
                }
                });
        }
    }

    template<class F, class... Args>
    auto Enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>
    {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) {
                throw std::runtime_error("Enqueue requested on a stopped ThreadPool");
            }

            active_tasks++; // 🚀 Increment counter when a job is queued
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // 🚀 Completely redesigned to accurately block until ALL threads are idle
    void WaitForAll() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        wait_condition.wait(lock, [this]() {
            return active_tasks == 0;
            });
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    std::condition_variable wait_condition; // 🚀 Separate condition variable for synchronization
    std::atomic<size_t> active_tasks;        // 🚀 Keeps track of queued/running tasks
    bool stop;
};