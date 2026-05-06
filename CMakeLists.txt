cmake_minimum_required(VERSION 3.14)
project(guid_race C)

set(CMAKE_C_STANDARD 11)

# Detect architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    set(ARCH_ARM64 TRUE)
    message(STATUS "Target: ARM64 (NEON)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|x86")
    set(ARCH_X86 TRUE)
    message(STATUS "Target: x86_64 (SSE2/SSSE3)")
else()
    message(STATUS "Target: ${CMAKE_SYSTEM_PROCESSOR} (scalar only)")
endif()

set(COMMON_SOURCES
    impl_baseline.c
    impl_dave.c
    impl_lookup16.c
    impl_unrolled.c
    impl_arithmetic.c
    impl_swar.c
    harness.c
)

if(ARCH_ARM64)
    list(APPEND COMMON_SOURCES impl_neon.c)
    add_compile_definitions(HAS_NEON=1)
elseif(ARCH_X86)
    list(APPEND COMMON_SOURCES impl_sse.c)
    add_compile_definitions(HAS_SSE=1)
    if(MSVC)
        # MSVC doesn't need -msse2/-mssse3 flags
    else()
        add_compile_options(-mssse3)
    endif()
endif()

add_executable(guid_race ${COMMON_SOURCES})

if(NOT MSVC)
    target_compile_options(guid_race PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)
    if(ARCH_ARM64 AND NOT APPLE)
        target_compile_options(guid_race PRIVATE -march=armv8-a)
    elseif(ARCH_ARM64 AND APPLE)
        target_compile_options(guid_race PRIVATE -march=native)
    elseif(ARCH_X86)
        target_compile_options(guid_race PRIVATE -march=native)
    endif()
else()
    target_compile_options(guid_race PRIVATE /O2 /W3)
endif()

# Platform-specific timer library
if(APPLE)
    # mach_absolute_time is in libSystem, linked by default
elseif(UNIX)
    target_link_libraries(guid_race PRIVATE rt)
endif()
