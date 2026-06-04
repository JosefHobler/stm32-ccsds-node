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

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

# --- ccsds library ---------------------------------------------------------
add_library(ccsds STATIC
    src/codec.cpp
)
target_include_directories(ccsds PUBLIC include)
target_compile_features(ccsds PUBLIC cxx_std_20)

# --- tests -----------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(codec_test tests/codec_test.cpp)
    target_link_libraries(codec_test PRIVATE ccsds Catch2::Catch2WithMain)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(codec_test)
endif()
