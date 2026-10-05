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
# GLM só de cabeçalhos; nem toda distribuição instala o glmConfig.cmake.
find_path(GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)
add_library(glm_headers INTERFACE)
target_include_directories(glm_headers SYSTEM INTERFACE ${GLM_INCLUDE_DIR})

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# render: GL, câmera, desenho. Não conhece SDL.
add_library(dr2render STATIC src/render/gl.cpp)
target_include_directories(dr2render PUBLIC src)
target_link_libraries(dr2render PUBLIC GLEW::GLEW OpenGL::GL glm_headers)

add_executable(viewer3d src/app/main.cpp)
target_link_libraries(viewer3d PRIVATE dr2render SDL3::SDL3)

add_executable(camera_test tests/camera_test.cpp)
target_include_directories(camera_test PRIVATE src)
target_link_libraries(camera_test PRIVATE glm_headers)
