cmake_minimum_required(VERSION 3.15)
project(cgr
    VERSION 0.0.1
    DESCRIPTION "Computer Graphics Renderer"
    LANGUAGES CXX
)
include(FetchContent)


option(ENABLE_SANITIZERS "Enable sanitizers" OFF)
option(ENABLE_TRACY "Enable Tracy profiler" OFF)
option(ENABLE_VULKAN_VALIDATION "Enable Vulkan validation layers" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# system dependencies
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

# 3pp dependencies
FetchContent_Declare (
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

add_executable(cgr)

target_link_libraries(cgr PRIVATE
    TracyClient
)
FetchContent_MakeAvailable(tracy)

if(ENABLE_TRACY)
    target_compile_definitions(cgr PRIVATE
        TRACY_ENABLE
        TRACY_CALLSTACK=2
    )
    target_link_libraries(cgr PRIVATE
        TracyClient
    )
endif()

file(GLOB_RECURSE SRC_FILES
    CONFIGURE_DEPENDS
    "src/*.cc"
    "include/*.h"
)

file(GLOB_RECURSE VENDOR_FILES
    CONFIGURE_DEPENDS
    "vendor/src/*.cc"
    "vendor/include/*.h"
)

target_include_directories(cgr PRIVATE
    "src/"
    "vendor/include/"
)

target_sources(cgr PRIVATE
    ${SRC_FILES}
    ${VENDOR_FILES}
)

target_link_libraries(cgr PRIVATE
    Vulkan::Vulkan
    glfw
)

if (ENABLE_SANITIZERS)
    target_compile_options(cgr PRIVATE
        "-fsanitize=address,undefined"
    )
    target_link_options(cgr PRIVATE
        "-fsanitize=address,undefined"
    )
endif()
