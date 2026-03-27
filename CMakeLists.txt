cmake_minimum_required(VERSION 3.10)
project(DataStreaming CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DEBUG_BUILD "Enable debug logging" OFF)
if(DEBUG_BUILD)
    add_compile_definitions(DEBUG_BUILD)
    message(STATUS "Debug logging: ON")
endif()

add_compile_definitions(LOG_DIR="${CMAKE_SOURCE_DIR}/logs")


# Richiesta delle librerie necessarie
find_package(cppzmq REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Catch2 3 REQUIRED)

# Abilita testing
enable_testing()

# Eseguibile principale (orchestratore)
add_executable(start
    main.cpp
    common/threadPool.cpp
    common/utils.cpp
    common/logger.cpp
    common/metrics.cpp
)
target_include_directories(start PRIVATE include)
target_link_libraries(start PRIVATE cppzmq)

# Aggiungi i processi come subdirectory
add_subdirectory(sender)
add_subdirectory(workerA)
add_subdirectory(workerB)
add_subdirectory(sink)
add_subdirectory(tests)
