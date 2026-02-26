add_executable(workerA 
    workerA.cpp
    ${CMAKE_SOURCE_DIR}/common/threadPool.cpp
    ${CMAKE_SOURCE_DIR}/common/utils.cpp
)

target_include_directories(workerA PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(workerA PRIVATE cppzmq)
