cmake_minimum_required(VERSION 3.20)

project(EasyWindows LANGUAGES CXX RC)

add_executable(EasyWindows WIN32 
    main.cpp
    DebugConsole.cpp
    EasyWindows.rc
 )

target_compile_features(EasyWindows PRIVATE cxx_std_17)

target_compile_definitions(EasyWindows PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

target_link_libraries(EasyWindows PRIVATE
    user32
    shell32
)