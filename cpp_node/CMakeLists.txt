cmake_minimum_required(VERSION 3.15)
project(antchain_node_cpp CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(monocypher STATIC
    third_party/monocypher/monocypher.c
    third_party/monocypher/monocypher-ed25519.c
)
target_include_directories(monocypher PUBLIC third_party/monocypher)

add_library(antchain_node_core
    src/json.cpp
    src/u256.cpp
    src/crypto.cpp
    src/transaction.cpp
    src/tsp.cpp
    src/consensus.cpp
    src/block.cpp
    src/chain.cpp
    src/mempool.cpp
    src/miner.cpp
    src/network.cpp
    src/node.cpp
    src/rpc.cpp
)
target_include_directories(antchain_node_core PUBLIC include)
target_link_libraries(antchain_node_core PUBLIC monocypher)

if(WIN32)
    target_link_libraries(antchain_node_core PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(antchain_node_core PUBLIC Threads::Threads)
endif()

add_executable(antchain_node src/main.cpp)
target_link_libraries(antchain_node PRIVATE antchain_node_core)

enable_testing()
add_executable(antchain_node_tests tests/test_main.cpp)
target_link_libraries(antchain_node_tests PRIVATE antchain_node_core)
add_test(NAME antchain_node_tests COMMAND antchain_node_tests)
