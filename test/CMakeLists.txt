add_executable(console console.cpp)
target_include_directories(console PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(console WebSocketAsio-${arch})

add_test(NAME console COMMAND console)
