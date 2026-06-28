add_executable(workerB 
    workerB.cpp
    ${CMAKE_SOURCE_DIR}/common/threadPool.cpp
    ${CMAKE_SOURCE_DIR}/common/utils.cpp
    ${CMAKE_SOURCE_DIR}/common/logger.cpp
    ${CMAKE_SOURCE_DIR}/common/config.cpp
)

target_include_directories(workerB PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(workerB PRIVATE cppzmq yaml-cpp::yaml-cpp)
