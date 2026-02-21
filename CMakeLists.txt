cmake_minimum_required(VERSION 3.10)
project(DataStreaming CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Trova la libreria ZeroMQ
find_package(cppzmq REQUIRED)

# Eseguibile principale (orchestratore)
add_executable(start
    main.cpp
    common/threadPool.cpp
)
target_include_directories(start PRIVATE include)
target_link_libraries(start PRIVATE cppzmq)

# Aggiungi i processi come subdirectory
add_subdirectory(sender)
add_subdirectory(workerA)
add_subdirectory(workerB)
add_subdirectory(sink)
