project(xq VERSION 0.0.6)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBRARY_OUTPUT_PATH ${main_SOURCE_DIR}/lib)

file(GLOB_RECURSE THIRD_HEADER ${main_SOURCE_DIR}/include/xq/third/*.hpp ${main_SOURCE_DIR}/include/xq/third/*.h)
file(GLOB_RECURSE NET_HEADER ${main_SOURCE_DIR}/include/xq/net/*.hpp ${main_SOURCE_DIR}/include/xq/net/*.h)
source_group(TREE ${CMAKE_SOURCE_DIR}/include FILES ${THIRD_HEADER} ${NET_HEADER})
INCLUDE_DIRECTORIES(${main_SOURCE_DIR}/include)
aux_source_directory(${main_SOURCE_DIR}/src/xq/third 3RD_LIST)
aux_source_directory(${main_SOURCE_DIR}/src/xq/net NET_LIST)

IF (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "--std=gnu++2a")
    add_definitions("-Wall -Werror -O3")
ELSE ()
    add_definitions("/utf-8 /std:c++17")
ENDIF (CMAKE_SYSTEM_NAME MATCHES "Linux")

add_library(xq ${3RD_LIST} ${NET_LIST} ${THIRD_HEADER} ${NET_HEADER})