cmake_minimum_required(VERSION 3.20)
project(AudioAshaSender VERSION 1.7.0 LANGUAGES C RC)

add_executable(AudioAshaSender WIN32
    main.c
    direct_transport.c
    direct_transport.h
    protocol.h
    resources.rc
    resource.h
)

set_target_properties(AudioAshaSender PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

target_compile_definitions(AudioAshaSender PRIVATE
    COBJMACROS
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(AudioAshaSender PRIVATE /W4 /utf-8)
else()
    target_compile_options(AudioAshaSender PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MINGW)
    target_link_options(AudioAshaSender PRIVATE -municode)
endif()

target_link_libraries(AudioAshaSender PRIVATE
    avrt
    ole32
    uuid
    ws2_32
    dwmapi
    uxtheme
    comctl32
    shell32
    gdi32
)
