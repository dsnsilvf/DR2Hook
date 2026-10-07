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
find_package(Threads REQUIRED)
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
    src/core/grid.cpp
    src/core/track.cpp
)
target_include_directories(dr2core PUBLIC src)

# edit: histórico, edits.json e alinhar ao terreno. Só dr2core: nada de GL.
add_library(dr2edit STATIC src/edit/history.cpp src/edit/edits_json.cpp src/edit/ground_fit.cpp)
target_include_directories(dr2edit PUBLIC src)
target_link_libraries(dr2edit PUBLIC dr2core)

# render: GL, câmera, desenho. Não conhece SDL.
add_library(dr2render STATIC
    src/render/gl.cpp
    src/render/track_shader.cpp
    src/render/terrain.cpp
    src/render/terrain_probe.cpp
    src/render/texture.cpp
    src/render/lines.cpp
    src/render/instances.cpp
    src/render/pick.cpp
)
target_include_directories(dr2render PUBLIC src)
target_link_libraries(dr2render PUBLIC dr2core GLEW::GLEW OpenGL::GL glm_headers Threads::Threads PRIVATE PkgConfig::WEBP)

# Dear ImGui 1.91.9b (third_party/imgui/README.md) com os backends SDL3 e OpenGL3: os painéis do app.
# Sem os avisos do projeto (código de terceiros, sem mudança).
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_sdl3.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui SYSTEM PUBLIC third_party/imgui third_party/imgui/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_options(imgui PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>)
target_link_libraries(imgui PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})

add_executable(viewer3d src/app/main.cpp src/app/track_view.cpp src/app/ui.cpp)
target_link_libraries(viewer3d PRIVATE dr2render dr2edit imgui SDL3::SDL3)

add_executable(camera_test tests/camera_test.cpp)
target_include_directories(camera_test PRIVATE src)
target_link_libraries(camera_test PRIVATE glm_headers)

add_executable(edit_test tests/edit_test.cpp)
target_link_libraries(edit_test PRIVATE dr2edit)

add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE dr2core)

# Regra de camadas: dr2core não liga nada.
get_target_property(_dr2core_links dr2core LINK_LIBRARIES)
if(_dr2core_links)
    message(FATAL_ERROR "dr2core não pode ligar outras bibliotecas: ${_dr2core_links}")
endif()
