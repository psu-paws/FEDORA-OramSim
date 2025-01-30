cmake_minimum_required(VERSION 3.22.1)

project(OramSimulator)

option(USE_ASAN "Enables -fstanitize=address" OFF)
option(USE_USAN "Enables -fstanitize=undefined" OFF)
option(USE_PG "Enables -pg" OFF)
option(PROFILE_STASH_LOAD "Turns on access by access tracking of stash size." OFF)
option(PROFILE_TREE_LOAD "Turns on access by access tracking of tree load by level" OFF)
option(PROFILE_TREE_LOAD_EXTENDED "Turns on super detailed tree load logging. Will probally kill performance." OFF)

set(CMAKE_CXX_STANDARD 20)

add_compile_options("-Wall")
add_link_options("-Wall")

add_compile_options("-march=native")
add_link_options("-march=native")

add_compile_options("-ffast-math")


if (USE_ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

if (USE_USAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

if (USE_PG)
    add_compile_options(-pg)
    add_link_options(-pg)
endif()

if (PROFILE_STASH_LOAD)
    add_compile_definitions(PROFILE_STASH_LOAD)
endif()

if (PROFILE_TREE_LOAD)
    add_compile_definitions(PROFILE_TREE_LOAD)
endif()

if (PROFILE_TREE_LOAD_EXTENDED)
    add_compile_definitions(PROFILE_TREE_LOAD_EXTENDED)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ABSL_PROPAGATE_CXX_STD ON)

set(BUILD_TESTING ON)
enable_testing()

include_directories(${PROJECT_SOURCE_DIR}/include)

# absl
add_subdirectory(${PROJECT_SOURCE_DIR}/external/abseil-cpp)

# cxxopts
add_subdirectory(${PROJECT_SOURCE_DIR}/external/cxxopts)

# toml++
add_subdirectory(${PROJECT_SOURCE_DIR}/external/tomlplusplus)

# google test
add_subdirectory(${PROJECT_SOURCE_DIR}/external/googletest)
include(GoogleTest)

# add the source directory
add_subdirectory(${PROJECT_SOURCE_DIR}/src)

# test sources
# enable_testing()
# add_subdirectory(${PROJECT_SOURCE_DIR}/tests)

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3")

message("CMAKE_C_FLAGS_DEBUG is ${CMAKE_C_FLAGS_DEBUG}")
message("CMAKE_C_FLAGS_RELEASE is ${CMAKE_C_FLAGS_RELEASE}")
message("CMAKE_C_FLAGS_RELWITHDEBINFO is ${CMAKE_C_FLAGS_RELWITHDEBINFO}")
message("CMAKE_C_FLAGS_MINSIZEREL is ${CMAKE_C_FLAGS_MINSIZEREL}")

message("CMAKE_CXX_FLAGS_DEBUG is ${CMAKE_CXX_FLAGS_DEBUG}")
message("CMAKE_CXX_FLAGS_RELEASE is ${CMAKE_CXX_FLAGS_RELEASE}")
message("CMAKE_CXX_FLAGS_RELWITHDEBINFO is ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message("CMAKE_CXX_FLAGS_MINSIZEREL is ${CMAKE_CXX_FLAGS_MINSIZEREL}")