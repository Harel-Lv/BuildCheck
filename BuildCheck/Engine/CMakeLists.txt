add_executable(engine_server
    src/main.cpp
    src/routes/register_routes.cpp
    src/routes/analyze_route.cpp
)

target_include_directories(engine_server PRIVATE include)

target_compile_definitions(engine_server PRIVATE
    CPPHTTPLIB_NO_MMAP
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

if (WIN32)
  target_link_libraries(engine_server PRIVATE ws2_32)
endif()
