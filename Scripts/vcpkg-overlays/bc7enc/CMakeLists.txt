# Build file for the bc7enc overlay port. Upstream ships a CMakeLists that builds its `test` tool and
# pulls in lodepng; we want only the encoder, so the port drops this one in its place.
cmake_minimum_required(VERSION 3.16)
project(bc7enc C)

add_library(bc7enc STATIC bc7enc.c)
target_include_directories(bc7enc PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<INSTALL_INTERFACE:include>)
set_target_properties(bc7enc PROPERTIES C_STANDARD 99)

install(TARGETS bc7enc EXPORT bc7enc-targets ARCHIVE DESTINATION lib)
install(FILES bc7enc.h DESTINATION include)
install(EXPORT bc7enc-targets NAMESPACE bc7enc:: FILE bc7enc-config.cmake DESTINATION share/bc7enc)
