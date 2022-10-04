cmake_minimum_required(VERSION 3.23)
project(CelebrationASIO)
set(CMAKE_CXX_STANDARD 17)


include_directories(CelebrationASIO
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include)


link_directories(${PROJECT_SOURCE_DIR}/lib)

add_executable(CelebrationASIO main.cpp ${SOURCES})


target_link_libraries(CelebrationASIO ASIO libremidi winmm pthread)


