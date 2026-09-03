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

class ThreadPool {
public:
    ThreadPool(size_t threads) : stop(false), active_tasks(0) {
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

                        // Safely track that a worker has begun processing
                        this->active_tasks++;
                    }

                    task(); // Execute the chunk generation job

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->active_tasks--;
                        // Only notify if NO tasks are running AND the queue is empty
                        if (this->active_tasks == 0 && this->tasks.empty()) {
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

    void WaitForAll() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        wait_condition.wait(lock, [this]() {
            return active_tasks == 0 && tasks.empty();
            });
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    std::condition_variable wait_condition;
    size_t active_tasks; // Safely protected by queue_mutex     
    bool stop;
};