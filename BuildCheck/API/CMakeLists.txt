cmake_minimum_required(VERSION 3.16)
project(api_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(api_server
    src/main.cpp
    src/routes/register_routes.cpp
    src/routes/analyze_route.cpp
    src/services/engine_client.cpp
    src/utils/json.cpp
)

target_include_directories(api_server PRIVATE include)

if (WIN32)
  target_compile_definitions(api_server PRIVATE
    CPPHTTPLIB_NO_MMAP
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
  )
  target_link_libraries(api_server PRIVATE ws2_32)
endif()
