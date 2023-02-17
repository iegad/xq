project(server)

add_executable(${PROJECT_NAME} server.cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EXECUTABLE_OUTPUT_PATH ${main_SOURCE_DIR})

INCLUDE_DIRECTORIES(${main_SOURCE_DIR}/src)
add_compile_options(-std=c++17 -fpermissive -O3)

target_link_libraries(${PROJECT_NAME} PUBLIC xq pthread)