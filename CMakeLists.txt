cmake_minimum_required(VERSION 3.20)
project(dr2hook LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Garantir que o projeto seja compilado como biblioteca compartilhada (DLL)
add_library(dxgi SHARED
    src/proxy/dxgi_proxy.cpp
    src/core/main.cpp
    src/core/hooks.cpp
    src/core/memory.cpp
    src/core/safety.cpp
)

target_include_directories(dxgi PRIVATE
    include
    src
)

# Definições específicas de Windows / MSVC
if(MSVC)
    target_compile_definitions(dxgi PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()
