# src/CMakeLists.txt - Build the libsocket library

# Create the libsocket library (STATIC or SHARED depending on your needs)
add_library(libsocket
    socket.cpp
)

# Set C++ standard requirement for the library (C++17)
target_compile_features(libsocket PUBLIC cxx_std_17)

# Define the public headers (located in include/libsocket/)
target_include_directories(libsocket
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>  # For local builds
        $<INSTALL_INTERFACE:include>  # For installed headers
)

# Platform-specific libraries for Windows
if(WIN32)
    target_link_libraries(libsocket PUBLIC ws2_32 iphlpapi)
endif()

# Use CMake's install command to specify where the library and headers should be placed when installed
install(TARGETS libsocket
    EXPORT libsocketTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/libsocket  # Place header in include/libsocket/
)

# Export the targets to allow easy find_package usage
install(EXPORT libsocketTargets
    FILE libsocketTargets.cmake
    NAMESPACE libsocket::
    DESTINATION lib/cmake/libsocket
)

# Optionally, create a version file for the CMake package (useful for versioning)
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libsocketConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libsocketConfigVersion.cmake" DESTINATION lib/cmake/libsocket)

# Optional: Ensure that shared libraries are built (if BUILD_SHARED_LIBS is set)
if(BUILD_SHARED_LIBS)
    set_target_properties(libsocket PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON  # If targeting Windows, export all symbols for shared libraries
    )
endif()
