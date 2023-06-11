project(rux_server)

add_executable(${PROJECT_NAME} rux_server.cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EXECUTABLE_OUTPUT_PATH ${main_SOURCE_DIR})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

INCLUDE_DIRECTORIES(${main_SOURCE_DIR}/include)

target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads xq)
IF (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(${PROJECT_NAME} PUBLIC libjemalloc.a libspdlog.a dl)
    link_directories("/usr/local/lib")
    include_directories("/user/local/include")
    add_definitions("-Wall -Werror -O3")
    set(CMAKE_CXX_FLAGS "--std=gnu++2a")
ELSE()
add_definitions("/utf-8 /std:c++17")
ENDIF (CMAKE_SYSTEM_NAME MATCHES "Linux")