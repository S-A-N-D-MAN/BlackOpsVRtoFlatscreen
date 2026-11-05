cmake_minimum_required(VERSION 3.25)
project(BlackOpsVrCorePrototype VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "This prototype must be configured for Windows Win32/x86")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
    add_compile_options(/Brepro)
    add_link_options(/Brepro)
endif()

option(BLACKOPSVR_CORE_BUILD_TESTS "Build the inherited machine-only core tests" ON)
include(CTest)

# The exact beta.1 sources and their original target names are retained.
# No retail WaW, T4, launcher, hook-address, or game-source target is built.
add_library(wawvr_project_options INTERFACE)
target_compile_definitions(wawvr_project_options INTERFACE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(wawvr_project_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/EHsc;/utf-8;/Zc:__cplusplus>)

set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan ON CACHE BOOL "" FORCE)
set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SPECIFICATION OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/waw-beta1/third_party/openxr-sdk openxr-sdk-build EXCLUDE_FROM_ALL)
if(TARGET openxr_loader)
    # The SDK supplies its own default; match the host CRT explicitly.
    set_property(TARGET openxr_loader PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(WAWVR_BUILD_TESTS ${BLACKOPSVR_CORE_BUILD_TESTS})
add_subdirectory(vendor/waw-beta1/src/xr xr-core)
add_library(BlackOpsVR::XR ALIAS wawvr_xr)

add_library(blackopsvr_core_config INTERFACE)
target_include_directories(blackopsvr_core_config INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(blackopsvr_core_config INTERFACE BlackOpsVR::XR)
add_library(BlackOpsVR::CoreConfig ALIAS blackopsvr_core_config)

# Explicitly launched integration probe; never registered as an unattended test.
add_executable(BlackOpsVrSimulatorProbe harness/simulator_probe.cpp)
target_include_directories(BlackOpsVrSimulatorProbe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../mod/src/vr")
target_link_libraries(BlackOpsVrSimulatorProbe PRIVATE
    BlackOpsVR::CoreConfig d3d9 d3d11 dxgi user32)
