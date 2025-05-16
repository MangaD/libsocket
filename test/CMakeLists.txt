# test/CMakeLists.txt - Build test executables for libsocket

add_executable(client client.cpp)
target_link_libraries(client PRIVATE libsocket)

add_executable(server server.cpp)
target_link_libraries(server PRIVATE libsocket)

# GoogleTest-based unit tests
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(socket_gtest socket_gtest.cpp)
target_link_libraries(socket_gtest PRIVATE libsocket gtest_main)

include(GoogleTest)
gtest_discover_tests(socket_gtest)
