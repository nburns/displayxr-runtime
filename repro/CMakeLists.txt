cmake_minimum_required(VERSION 3.16)
project(refresh_rate_repro LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)

# OpenXR: a CONFIG package if present, else a loader lib + headers via
# OPENXR_INCLUDE_DIR / OPENXR_LOADER_DIR.
find_package(OpenXR QUIET CONFIG)
if(NOT OpenXR_FOUND)
    set(OPENXR_INCLUDE_DIR "" CACHE PATH "dir containing openxr/openxr.h")
    find_library(OPENXR_LOADER_LIB NAMES openxr_loader HINTS "$ENV{OPENXR_LOADER_DIR}")
    add_library(OpenXR::openxr_loader SHARED IMPORTED)
    set_target_properties(OpenXR::openxr_loader PROPERTIES IMPORTED_LOCATION "${OPENXR_LOADER_LIB}")
endif()

add_executable(refresh_rate_repro refresh_rate_repro.cpp)
target_include_directories(refresh_rate_repro PRIVATE ${OPENXR_INCLUDE_DIR})
target_link_libraries(refresh_rate_repro PRIVATE OpenXR::openxr_loader Vulkan::Vulkan)
