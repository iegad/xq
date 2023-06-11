cmake_minimum_required(VERSION 3.16)

project(main)
# Sladviski
add_subdirectory(${PROJECT_SOURCE_DIR}/src)
add_subdirectory(${PROJECT_SOURCE_DIR}/example/rux_server)
add_subdirectory(${PROJECT_SOURCE_DIR}/example/rux_client)
