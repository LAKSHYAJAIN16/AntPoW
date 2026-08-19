cmake_minimum_required(VERSION 3.15)
project(antchain_sim_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_library(antchain_core
    src/tsp.cpp
    src/consensus.cpp
    src/miner.cpp
    src/blockchain.cpp
    src/metrics.cpp
)
target_include_directories(antchain_core PUBLIC include)

add_executable(antchain_experiment src/experiment.cpp)
target_link_libraries(antchain_experiment PRIVATE antchain_core)

enable_testing()
add_executable(antchain_tests tests/test_main.cpp)
target_link_libraries(antchain_tests PRIVATE antchain_core)
add_test(NAME antchain_tests COMMAND antchain_tests)
