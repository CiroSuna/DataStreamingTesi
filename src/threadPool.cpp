#include <iostream>
#include <thread>
#include <mutex>
#include "threadPool.hpp"

// Constructor implementation
ThreadPool::ThreadPool(int initial_threads) 
    : active_threads{0}, destroy_count{0}, stop{false} {
    for (size_t i{}; i < initial_threads; ++i) {
        thrd_list.emplace_back(&ThreadPool::thrd_task_loop, this); 
    }
}

// Shutdown implementation
void ThreadPool::shutdown() {
    std::unique_lock<std::mutex> lock(pool_mtx);
    stop = true;
}

// Destructor implementation
ThreadPool::~ThreadPool() {
    shutdown();
    pool_notify.notify_all();
    for (size_t i{0}; i < thrd_list.size(); ++i) {
        if (thrd_list[i].joinable())
            thrd_list[i].join();
    }
}

// Add task implementation
void ThreadPool::add_task(std::function<void()> f) {
    std::unique_lock<std::mutex> lock{pool_mtx};
    task_queue.push(f);
    pool_notify.notify_all();
}

// Fetch task implementation (MUST be called while holding pool_mtx lock)
std::function<void()> ThreadPool::fetch_task_unlocked() {
    if (task_queue.empty()) {
        return nullptr;
    }

    // std::move used for efficiency, not creating a copy but just moving the data
    auto task{std::move(task_queue.front())}; 
    task_queue.pop();
    return task;
}

// Destroy threads implementation
void ThreadPool::destroy_n_threads(int n) {
    std::unique_lock<std::mutex> lock(pool_mtx);
    destroy_count += n; 
}
       
// Loop for task fetching
void ThreadPool::thrd_task_loop() {
    while (true) {
        std::function<void()> curr_task{};

        { 
            std::unique_lock<std::mutex> lock(pool_mtx);
            pool_notify.wait(lock, [this]() {
                return !task_queue.empty() || stop;
            });

            if (stop || destroy_count > 0) {
                if (destroy_count > 0)
                    destroy_count--;
                return;
            }
            
            // Fetch task using helper (we hold the lock)
            curr_task = fetch_task_unlocked();
            if (curr_task) {
                active_threads++;
            }
        }
        
        if (curr_task) {
            try {
                curr_task();
            } catch (const std::exception& e) {
                std::cerr << "Errore esecizione task " << e.what() << '\n';
            } catch (...) {
                std::cerr << "Errore sconosciuto\n";
            }

            {     
                std::unique_lock<std::mutex> lock(pool_mtx);
                active_threads--;
            }
        }
    }
}

