project(xq VERSION 0.0.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBRARY_OUTPUT_PATH ${main_SOURCE_DIR}/lib)

add_compile_options(-std=c++17 -fpermissive -O3)
aux_source_directory(${main_SOURCE_DIR}/src/xq/third SRC_LIST)

add_library(xq ${SRC_LIST})