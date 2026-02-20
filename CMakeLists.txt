cmake_minimum_required(VERSION 3.10)
project(DataStreaming CXX)

# Imposta lo standard C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Aggiungi i file sorgenti
add_executable(start
    src/main.cpp
    src/threadPool.cpp
)

# Includi la directory per gli header
target_include_directories(start PRIVATE include)
