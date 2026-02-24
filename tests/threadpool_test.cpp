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
}
