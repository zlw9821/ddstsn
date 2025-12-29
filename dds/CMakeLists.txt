cmake_minimum_required(VERSION 3.16)

project(dds_over_tsn_dds
    VERSION 0.1.0
    LANGUAGES CXX
)

# =========================
# C++ settings
# =========================
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =========================
# Fast DDS
# =========================
find_package(fastdds REQUIRED CONFIG)

# =========================
# DDS generated types
# =========================
add_library(dds_types STATIC)

# 自动收集 fastddsgen 生成的源文件
file(GLOB_RECURSE DDS_TYPE_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/*.cxx
)

target_sources(dds_types
    PRIVATE
        ${DDS_TYPE_SOURCES}
)

target_include_directories(dds_types
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
)

target_link_libraries(dds_types
    PUBLIC
        fastdds
)

# =========================
# Publisher
# =========================
add_executable(dds_pub)

target_sources(dds_pub
    PRIVATE
        src/dds_pub.cpp
)

target_link_libraries(dds_pub
    PRIVATE
        dds_types
)

# =========================
# Subscriber
# =========================
add_executable(dds_sub)

target_sources(dds_sub
    PRIVATE
        src/dds_sub.cpp
)

target_link_libraries(dds_sub
    PRIVATE
        dds_types
)
