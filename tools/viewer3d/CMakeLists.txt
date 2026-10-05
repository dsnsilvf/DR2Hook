cmake_minimum_required(VERSION 3.20)
project(dr2viewer3d CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Tipo de build" FORCE)
endif()

find_package(SDL3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
# GLM só de cabeçalhos; nem toda distribuição instala o glmConfig.cmake.
find_path(GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)
add_library(glm_headers INTERFACE)
target_include_directories(glm_headers SYSTEM INTERFACE ${GLM_INCLUDE_DIR})

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# core: formatos exportados pelo Python. Só a biblioteca padrão: nada de SDL, GL, GLEW ou GLM.
add_library(dr2core STATIC
    src/core/io.cpp
    src/core/json.cpp
    src/core/dr2m.cpp
    src/core/dr2i.cpp
    src/core/track.cpp
)
target_include_directories(dr2core PUBLIC src)

# render: GL, câmera, desenho. Não conhece SDL.
add_library(dr2render STATIC
    src/render/gl.cpp
    src/render/track_shader.cpp
    src/render/terrain.cpp
    src/render/texture.cpp
    src/render/lines.cpp
    src/render/instances.cpp
)
target_include_directories(dr2render PUBLIC src)
target_link_libraries(dr2render PUBLIC dr2core GLEW::GLEW OpenGL::GL glm_headers PRIVATE PkgConfig::WEBP)

add_executable(viewer3d src/app/main.cpp src/app/track_view.cpp)
target_link_libraries(viewer3d PRIVATE dr2render SDL3::SDL3)

add_executable(camera_test tests/camera_test.cpp)
target_include_directories(camera_test PRIVATE src)
target_link_libraries(camera_test PRIVATE glm_headers)

add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE dr2core)

# Regra de camadas: dr2core não liga nada.
get_target_property(_dr2core_links dr2core LINK_LIBRARIES)
if(_dr2core_links)
    message(FATAL_ERROR "dr2core não pode ligar outras bibliotecas: ${_dr2core_links}")
endif()
