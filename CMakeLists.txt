cmake_minimum_required(VERSION 3.20)

project(main)

add_subdirectory(${PROJECT_SOURCE_DIR}/src)
add_subdirectory(${PROJECT_SOURCE_DIR}/example/server)
add_subdirectory(${PROJECT_SOURCE_DIR}/example/client)
