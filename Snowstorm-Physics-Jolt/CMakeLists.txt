# Snowstorm-Physics-Jolt: the physics module (IModule "PhysicsJolt") — Jolt Physics bound to the ECS.
# A static lib like Core; executables link it WHOLE_ARCHIVE so its component registrations survive.
add_library(Snowstorm-Physics-Jolt STATIC)

set_target_properties(Snowstorm-Physics-Jolt PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

file(GLOB_RECURSE PHYSICS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp
)
target_sources(Snowstorm-Physics-Jolt PRIVATE ${PHYSICS_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source PREFIX "Source" FILES ${PHYSICS_SOURCES})

find_package(Jolt CONFIG REQUIRED)

# Jolt::Jolt carries its JPH_* configuration as INTERFACE compile definitions, so every TU that includes
# Jolt headers (here and in the executables) sees exactly the flags the library was built with — the
# ABI-matching requirement Jolt documents (JPH_DEBUG_RENDERER, double precision, object layer bits).
target_link_libraries(Snowstorm-Physics-Jolt PUBLIC Snowstorm-Core Jolt::Jolt)
target_include_directories(Snowstorm-Physics-Jolt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
