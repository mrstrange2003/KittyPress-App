cmake_minimum_required(VERSION 3.18)
project("kittypress")

set(CMAKE_CXX_STANDARD 17)

file(GLOB SRC_FILES "${CMAKE_SOURCE_DIR}/*.cpp")

# ---- zstd sources ----
file(GLOB ZSTD_COMMON external/zstd/lib/common/*.c)
file(GLOB ZSTD_COMPRESS external/zstd/lib/compress/*.c)
file(GLOB ZSTD_DECOMPRESS external/zstd/lib/decompress/*.c)

add_library(
        kittypress
        SHARED
        ${SRC_FILES}
        ${ZSTD_COMMON}
        ${ZSTD_COMPRESS}
        ${ZSTD_DECOMPRESS}
)

target_include_directories(
        kittypress
        PRIVATE
        ${CMAKE_SOURCE_DIR}
        external/zstd/lib
)

find_library(log-lib log)

target_link_libraries(
        kittypress
        PRIVATE
        ${log-lib}
)
