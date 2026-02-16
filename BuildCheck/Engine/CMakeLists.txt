cmake_minimum_required(VERSION 3.16)
project(engine_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(engine_server
    src/main.cpp
    src/routes/register_routes.cpp
    src/routes/analyze_route.cpp
)

target_include_directories(engine_server PRIVATE include)

if (WIN32)
  target_compile_definitions(engine_server PRIVATE
    CPPHTTPLIB_NO_MMAP
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
  )
  target_link_libraries(engine_server PRIVATE ws2_32)
endif()
