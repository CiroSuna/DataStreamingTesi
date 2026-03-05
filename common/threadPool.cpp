#include <iostream>
#include <thread>
#include <mutex>
#include <algorithm>
#include "threadPool.hpp"

ThreadPool::ThreadPool(int initial_threads) 
    : active_threads{0}, destroy_count{0}, stop{false} {
    for (size_t i{}; i < initial_threads; ++i) {
        thrd_list.emplace_back(&ThreadPool::thrd_task_loop, this); 
    }
}

void ThreadPool::shutdown() {
    std::unique_lock<std::mutex> lock(pool_mtx);
    stop = true;
}

ThreadPool::~ThreadPool() {
    shutdown();
    pool_notify.notify_all();
    for (size_t i{0}; i < thrd_list.size(); ++i) {
        if (thrd_list[i].joinable())
            thrd_list[i].join();
    }
}

// Fetch task implementation (MUST be called while holding pool_mtx lock)
std::function<void()> ThreadPool::fetch_task_unlocked() {
    if (task_queue.empty()) {
        return nullptr;
    }

    auto task{std::move(task_queue.front())}; 
    task_queue.pop();
    return task;
}

// Destroy threads implementation
void ThreadPool::destroy_n_threads(int n) {
    std::unique_lock<std::mutex> lock(pool_mtx);
    destroy_count += n; 
}
      
// Cleanup finished threads (MUST be called while holding pool_mtx lock)
void ThreadPool::cleanup_finished_threads() {
    for (auto& id : finished_thread_ids) {
        auto it = std::find_if(thrd_list.begin(), thrd_list.end(),
            [&id](std::thread& t){ return t.get_id() == id; });
        if (it != thrd_list.end()) {
            it->join();
            thrd_list.erase(it);
        }
    }
    finished_thread_ids.clear();
}

void ThreadPool::add_n_threads(int n) {
    std::unique_lock<std::mutex> lock(pool_mtx);
    if (stop) return;
    cleanup_finished_threads();
    for (size_t i {0}; i < n; i++) {
        thrd_list.emplace_back(&ThreadPool::thrd_task_loop, this); 
    } 
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
                if (destroy_count > 0) {
                    destroy_count--;
                    finished_thread_ids.push_back(std::this_thread::get_id());
                }
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

