project(xq VERSION 0.0.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBRARY_OUTPUT_PATH ${main_SOURCE_DIR}/lib)


aux_source_directory(${main_SOURCE_DIR}/src/xq/third SRC_LIST)

IF (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "--std=gnu++2a")
    add_definitions("-Wall -Werror -O3")
ELSE ()
    add_definitions("/utf-8 /std:c++17")
ENDIF (CMAKE_SYSTEM_NAME MATCHES "Linux")

add_library(xq ${SRC_LIST})