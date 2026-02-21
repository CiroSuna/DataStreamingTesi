add_executable(sender sender.cpp)

target_include_directories(sender PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(sender PRIVATE cppzmq)
