cmake_minimum_required(VERSION 3.16)
project(snap VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static link on Windows for portable binary
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /utf-8)
elseif(MINGW)
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# Static link on Linux for portable binary
if(UNIX AND NOT APPLE)
    add_link_options(-static-libgcc -static-libstdc++)
endif()

add_executable(snap src/main.cpp)

# Link required libraries
if(WIN32)
    target_link_libraries(snap PRIVATE advapi32 user32 shell32)
endif()

# Install target
install(TARGETS snap RUNTIME DESTINATION bin)
