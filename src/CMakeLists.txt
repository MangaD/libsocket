# src/CMakeLists.txt - Build the libsocket library

add_library(libsocket
    socket.cpp
)

# Public headers
set_target_properties(libsocket PROPERTIES PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/socket.hpp")

target_include_directories(libsocket
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
           $<INSTALL_INTERFACE:include>
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(libsocket PUBLIC ws2_32 iphlpapi)
endif()

# Install the library and headers
install(TARGETS libsocket
    EXPORT libsocketTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include
)
