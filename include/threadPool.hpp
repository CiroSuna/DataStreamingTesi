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
        // Helper function - must be called while holding pool_mtx lock
        std::function<void()> fetch_task_unlocked();

    public:
        ThreadPool(int initial_threads);
        void shutdown();
        ~ThreadPool();
        void add_task(std::function<void()> f);
        void destroy_n_threads(int n);
};

#endif