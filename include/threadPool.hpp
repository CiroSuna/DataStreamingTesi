#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>
#include <condition_variable>

class ThreadPool {
    private:
        std::vector<std::thread> thrd_list;
        std::queue<std::function<void()>> task_queue;
        int active_threads;
        int destroy_count;
        bool stop;
        std::mutex pool_mtx;
        std::condition_variable pool_notify;
        void thrd_task_loop();
        // Helper function - must be called while holding pool_mtx lock, remains void since lambda inside taks queue is [](){...} with no return
        std::function<void()> fetch_task_unlocked();

    public:
        ThreadPool(int initial_threads);
        void shutdown();
        ~ThreadPool();
        template<typename F, typename... Args>
        void add_task(F&& f, Args&&... args) {
            std::unique_lock<std::mutex> lock{pool_mtx};
            auto task = [f_forwarded = std::forward<F>(f), 
                         args_forwarded = std::make_tuple(std::forward<Args>(args)...)]() {
                std::apply(f_forwarded, args_forwarded);
            };
            task_queue.push(task);
            pool_notify.notify_all();
        }
        void destroy_n_threads(int n);
};

#endif