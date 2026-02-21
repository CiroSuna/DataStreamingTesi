add_executable(sink sink.cpp)

target_include_directories(sink PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(sink PRIVATE cppzmq)
