add_executable(sender sender.cpp
    ${CMAKE_SOURCE_DIR}/common/utils.cpp
    ${CMAKE_SOURCE_DIR}/common/logger.cpp
    ${CMAKE_SOURCE_DIR}/common/config.cpp
)

target_include_directories(sender PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(sender PRIVATE cppzmq yaml-cpp)
