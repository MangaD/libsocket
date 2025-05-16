# test/CMakeLists.txt - Build test executables for libsocket

# Add client and server executables
add_executable(client client.cpp)
target_link_libraries(client PRIVATE libsocket)

add_executable(server server.cpp)
target_link_libraries(server PRIVATE libsocket)

# GoogleTest-based unit tests
include(FetchContent)

# Fetch GoogleTest from GitHub (instead of downloading a ZIP file)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0  # Lock to a specific version
)

# Force shared CRT for GoogleTest
set(gtest_force_shared_crt ON CACHE BOOL "Force shared CRT for GoogleTest" FORCE)

# Make GoogleTest available for use
FetchContent_MakeAvailable(googletest)

# Add the test executable for socket_gtest
add_executable(socket_gtest socket_gtest.cpp)

# Link libsocket and GoogleTest to the test executable
target_link_libraries(socket_gtest PRIVATE libsocket gtest_main)

# Enable test discovery for GoogleTest
include(GoogleTest)
gtest_discover_tests(socket_gtest)

# Include directories for libsocket and GoogleTest headers
target_include_directories(socket_gtest PRIVATE
    ${CMAKE_SOURCE_DIR}/include      # Include the libsocket headers
    ${gtest_SOURCE_DIR}/googletest/include  # Include GoogleTest headers
    ${gtest_SOURCE_DIR}/googlemock/include # Include GoogleMock headers if needed
)
