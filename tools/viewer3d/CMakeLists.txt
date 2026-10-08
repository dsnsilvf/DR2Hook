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
    src/core/progress.cpp
    src/core/live.cpp
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
    src/render/car.cpp
)
target_include_directories(dr2render PUBLIC src)
target_link_libraries(dr2render PUBLIC dr2core GLEW::GLEW OpenGL::GL glm_headers Threads::Threads PRIVATE PkgConfig::WEBP)

# Dear ImGui 1.91.9b, ramo docking (third_party/imgui/README.md), com os backends SDL3 e OpenGL3: os painéis
# do app. Com o FreeType do sistema o texto sai com o rasterizador dele (hinting leve), mais nítido que o
# stb_truetype; sem ele, o ImGui usa o stb. O ImGuizmo (third_party/imguizmo/README.md) é o gizmo de mover e
# girar no 3D. Sem os avisos do projeto (código de terceiros, sem mudança).
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_sdl3.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
    third_party/imguizmo/ImGuizmo.cpp
)
target_include_directories(imgui SYSTEM PUBLIC third_party/imgui third_party/imgui/backends third_party/imguizmo)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_options(imgui PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>)
target_link_libraries(imgui PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})
find_package(Freetype)
if(FREETYPE_FOUND)
    target_sources(imgui PRIVATE third_party/imgui/misc/freetype/imgui_freetype.cpp)
    target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
    target_link_libraries(imgui PRIVATE Freetype::Freetype)
endif()

# Fontes do editor (assets/fonts/README.md) e a imagem do Sobre (assets/about) dentro do executável: o configure
# grava os bytes num .cpp gerado, e mudar um desses arquivos refaz o configure. Argumentos: pares nome (vira fonts::k<nome>) e arquivo.
function(dr2_embed_fonts out)
    set(args ${ARGN})
    string(REPEAT "0x[0-9a-f][0-9a-f]," 24 row)
    set(arrays "")
    set(blobs "")
    while(args)
        list(POP_FRONT args name file)
        set(path ${CMAKE_CURRENT_SOURCE_DIR}/${file})
        file(READ ${path} hex HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
        string(REGEX REPLACE "(${row})" "\\1\n    " bytes "${bytes}")
        string(APPEND arrays "const unsigned char k${name}Data[] = {\n    ${bytes}\n};\n\n")
        string(APPEND blobs "const Blob k${name}{k${name}Data, sizeof k${name}Data};\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${path})
    endwhile()
    file(WRITE ${out}.tmp
        "// Gerado pelo CMakeLists.txt a partir de assets/fonts e assets/about: não edite.\n"
        "#include \"app/embedded_fonts.hpp\"\n\n"
        "namespace dr2::app::fonts {\nnamespace {\n\n${arrays}}  // namespace\n\n${blobs}\n"
        "}  // namespace dr2::app::fonts\n")
    configure_file(${out}.tmp ${out} COPYONLY)
endfunction()
set(DR2_FONTS_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_fonts.cpp)
dr2_embed_fonts(${DR2_FONTS_CPP}
    InterRegular assets/fonts/Inter-Regular.ttf
    InterSemiBold assets/fonts/Inter-SemiBold.ttf
    Lucide assets/fonts/Lucide.ttf
    AboutImage assets/about/about.webp)

add_executable(viewer3d src/app/main.cpp src/app/track_view.cpp src/app/ui.cpp src/app/launch.cpp ${DR2_FONTS_CPP})
target_link_libraries(viewer3d PRIVATE dr2render dr2edit imgui SDL3::SDL3 PkgConfig::WEBP)

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
