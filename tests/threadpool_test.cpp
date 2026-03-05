#include <catch2/catch_all.hpp>
#include "threadPool.hpp"
TEST_CASE("ThreadPool funzionalità complete", "[threadpool]") {
    ThreadPool pool(2);
    
    SECTION("Aggiunta di un task") {
        int result = 0;
        pool.add_task([&result]() { result = 42; });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE(result == 42);
    }
    
    SECTION("Aggiunta di più task") {
        int counter = 0;
        for (int i = 0; i < 5; ++i) {
            pool.add_task([&counter]() { counter++; });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE(counter == 5);
    }

    SECTION("Test errore a runtime") {
        std::string start = "Hello";
        pool.add_task([&start](std::string a) {start = start + a;}, " world!");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE("Hello world!" == start);
    }
    SECTION("Test creazione e distruzione thread") {
        std::atomic<int> running{0};
        std::mutex m;
        std::condition_variable cv;
        bool release = false;

        // Sottometti 4 task che si bloccano finché non li rilasciamo
        for (int i = 0; i < 4; i++) {
            pool.add_task([&]() {
                ++running;
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&]{ return release; });
                --running;
            });
        }

        // Con 2 thread, solo 2 task possono girare contemporaneamente
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE(running.load() == 2);

        // Aggiunta di 2 thread -> ora 4 task girano in parallelo
        pool.add_n_threads(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE(running.load() == 4);

        // Rilascia tutti i task
        {
            std::lock_guard<std::mutex> lk(m);
            release = true;
        }
        cv.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
