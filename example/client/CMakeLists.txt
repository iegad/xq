project(client)

add_executable(${PROJECT_NAME} client.cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EXECUTABLE_OUTPUT_PATH ${main_SOURCE_DIR})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

INCLUDE_DIRECTORIES(${main_SOURCE_DIR}/src)
add_compile_options(-Wall -Werror -O3)

target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads xq)
IF (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(${PROJECT_NAME} PUBLIC libjemalloc.a)
    link_directories("/usr/local/lib")
    include_directories("/user/local/include")
ENDIF (CMAKE_SYSTEM_NAME MATCHES "Linux")