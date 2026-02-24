file(GLOB TEST_SOURCES "*.cpp")
add_executable(tests
    ${TEST_SOURCES}
    ${CMAKE_SOURCE_DIR}/common/threadPool.cpp
)

target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)
add_test(NAME AllTests COMMAND tests)
add_test(NAME ThreadPoolTests COMMAND tests "[threadpool]")