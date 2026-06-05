cmake_minimum_required(VERSION 3.20)

project(ccsds_cpp
    LANGUAGES CXX
    DESCRIPTION "Host-side modern C++20 CCSDS Space Packet codec"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(CCSDS_WITH_NATS  "Build the NATS TM/TC bridge (requires cnats)." OFF)
option(CCSDS_SANITIZE   "Enable Address+UndefinedBehavior sanitizers."  OFF)
option(CCSDS_BUILD_FUZZ "Build the libFuzzer entry point (clang only)." OFF)

# --- our warning flags (scoped per-target below, NOT project-wide) ---------
# Applying these project-wide pollutes the compile lines of FetchContent
# deps (cnats's test.c, in particular, trips dozens of -Wconversion /
# -Wshadow / -Wunused-parameter warnings that have nothing to do with us).
# Keeping them target-local means we still get strict checking on the code
# we own without forcing it on upstream.
if(MSVC)
    set(CCSDS_WARNINGS /W4 /permissive-)
else()
    set(CCSDS_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

# Sanitizers, on the other hand, *must* be project-wide so the runtime
# instrumentation matches across all linked TUs (mixing ASan-instrumented
# and uninstrumented code breaks shadow-memory invariants).
if(CCSDS_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# --- ccsds library ---------------------------------------------------------
set(CCSDS_SOURCES
    src/codec.cpp
    src/framer.cpp
)
if(CCSDS_WITH_NATS)
    list(APPEND CCSDS_SOURCES src/nats_bridge.cpp)
endif()

add_library(ccsds STATIC ${CCSDS_SOURCES})
target_include_directories(ccsds PUBLIC include)
target_compile_features(ccsds PUBLIC cxx_std_20)
target_compile_options(ccsds PRIVATE ${CCSDS_WARNINGS})

include(FetchContent)

# nlohmann/json is used by tests and (when enabled) by the NATS bridge.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

if(CCSDS_WITH_NATS)
    set(NATS_BUILD_WITH_TLS    OFF CACHE BOOL "" FORCE)
    set(NATS_BUILD_STREAMING   OFF CACHE BOOL "" FORCE)
    set(NATS_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
    set(NATS_BUILD_TLS_FORCE_HOST_VERIFY OFF CACHE BOOL "" FORCE)
    set(NATS_BUILD_LIB_STATIC  ON  CACHE BOOL "" FORCE)
    set(NATS_BUILD_LIB_SHARED  OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        cnats
        GIT_REPOSITORY https://github.com/nats-io/nats.c.git
        GIT_TAG        v3.8.2
    )
    # Use Populate + add_subdirectory(EXCLUDE_FROM_ALL) so cnats's own
    # test target (test/test.c — 30k+ lines, including JetStream/KV/
    # ObjectStore code paths) is not built unless something in our project
    # actually depends on it. We only link nats_static.
    FetchContent_GetProperties(cnats)
    if(NOT cnats_POPULATED)
        FetchContent_Populate(cnats)
        add_subdirectory(${cnats_SOURCE_DIR} ${cnats_BINARY_DIR}
                         EXCLUDE_FROM_ALL)
    endif()

    target_link_libraries(ccsds
        PUBLIC  nats_static
        PRIVATE nlohmann_json::nlohmann_json)
    # SYSTEM include: suppresses our warnings (`-Wconversion` in particular)
    # against cnats's headers when nats_bridge.cpp pulls them in.
    target_include_directories(ccsds SYSTEM PUBLIC
        ${cnats_SOURCE_DIR}/src
    )
endif()

# --- tests -----------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)

    set(CCSDS_TEST_SOURCES
        tests/codec_test.cpp
        tests/conformance_test.cpp
        tests/framer_test.cpp
        tests/differential_test.cpp
    )
    if(CCSDS_WITH_NATS)
        list(APPEND CCSDS_TEST_SOURCES tests/nats_bridge_test.cpp)
    endif()

    add_executable(codec_test ${CCSDS_TEST_SOURCES})
    target_include_directories(codec_test PRIVATE tests)
    target_compile_definitions(codec_test PRIVATE
        CCSDS_GENERATED_FIXTURES_JSON="${CMAKE_CURRENT_SOURCE_DIR}/tests/generated_fixtures.json"
    )
    target_compile_options(codec_test PRIVATE ${CCSDS_WARNINGS})
    target_link_libraries(codec_test
        PRIVATE ccsds Catch2::Catch2WithMain nlohmann_json::nlohmann_json)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    # Label our tests so CI can filter them with `ctest -L ccsds`. cnats's
    # CMakeLists registers ~300 of its own tests (Test_StanXxx, ...) via
    # add_test() at configure time. EXCLUDE_FROM_ALL stops the testsuite
    # binary from being *built*, but cnats's add_test() calls already ran,
    # so ctest will otherwise try to launch a binary that doesn't exist.
    catch_discover_tests(codec_test
        PROPERTIES LABELS "ccsds")
endif()

# --- fuzz target (libFuzzer; clang only) -----------------------------------
if(CCSDS_BUILD_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "CCSDS_BUILD_FUZZ=ON requires clang/clang++ (libFuzzer is "
            "shipped as part of clang's compiler-rt). Re-configure with "
            "-DCMAKE_CXX_COMPILER=clang++.")
    endif()
    add_executable(fuzz_decode fuzz/fuzz_decode.cpp)
    target_link_libraries(fuzz_decode PRIVATE ccsds)
    target_compile_options(fuzz_decode PRIVATE
        ${CCSDS_WARNINGS}
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(fuzz_decode PRIVATE
        -fsanitize=fuzzer,address,undefined)
endif()
