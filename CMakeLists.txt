cmake_minimum_required(VERSION 3.20)
project(dr2hook LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fontes do Lua 5.4 compilados como C++ para stack unwinding seguro (try/catch)
set(LUA_SOURCES
    vendor/lua/lapi.c
    vendor/lua/lauxlib.c
    vendor/lua/lbaselib.c
    vendor/lua/lcode.c
    vendor/lua/lcorolib.c
    vendor/lua/lctype.c
    vendor/lua/ldblib.c
    vendor/lua/ldebug.c
    vendor/lua/ldo.c
    vendor/lua/ldump.c
    vendor/lua/lfunc.c
    vendor/lua/lgc.c
    vendor/lua/linit.c
    vendor/lua/liolib.c
    vendor/lua/llex.c
    vendor/lua/lmathlib.c
    vendor/lua/lmem.c
    vendor/lua/loadlib.c
    vendor/lua/lobject.c
    vendor/lua/lopcodes.c
    vendor/lua/loslib.c
    vendor/lua/lparser.c
    vendor/lua/lstate.c
    vendor/lua/lstring.c
    vendor/lua/lstrlib.c
    vendor/lua/ltable.c
    vendor/lua/ltablib.c
    vendor/lua/ltm.c
    vendor/lua/lundump.c
    vendor/lua/lutf8lib.c
    vendor/lua/lvm.c
    vendor/lua/lzio.c
)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)

# Fontes do Dear ImGui (Core e Backends)
set(IMGUI_CORE_SOURCES
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
)

set(IMGUI_BACKEND_SOURCES
    vendor/imgui/backends/imgui_impl_win32.cpp
    vendor/imgui/backends/imgui_impl_dx11.cpp
)

# Garantir que o projeto seja compilado como biblioteca compartilhada (DLL)
add_library(dxgi SHARED
    src/proxy/dxgi_proxy.cpp
    src/core/main.cpp
    src/core/logger.cpp
    src/core/hooks.cpp
    src/core/memory.cpp
    src/core/safety.cpp
    src/core/player.cpp
    src/core/savestate.cpp
    src/ui/overlay.cpp
    src/script/lua_engine.cpp
    src/script/mod_manager.cpp
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_BACKEND_SOURCES}
    ${LUA_SOURCES}
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde32.c
    vendor/minhook/src/hde/hde64.c
)

target_include_directories(dxgi PRIVATE
    include
    src
    vendor/lua
    vendor/imgui
    vendor/imgui/backends
    vendor/minhook/include
)

if(NOT WIN32 AND EXISTS "/usr/include/wine/windows")
    target_include_directories(dxgi PRIVATE /usr/include/wine/windows)
    target_compile_definitions(dxgi PRIVATE
        WINE_UNICODE_NATIVE
        _DPI_AWARENESS_CONTEXTS_
    )
endif()

# Definições específicas de Windows / MSVC
if(WIN32 OR MSVC)
    target_compile_definitions(dxgi PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        DR2HOOK_USE_BACKENDS
    )
    target_compile_options(dxgi PRIVATE "-Dlocaltime_r(t,b)=(localtime_s((b),(t)),(b))")
    if(MINGW)
        target_link_options(dxgi PRIVATE "-Wl,--defsym,_ZN7dr2hook30ImGui_ImplWin32_WndProcHandlerEP6HWND__jyx=_Z30ImGui_ImplWin32_WndProcHandlerP6HWND__jyx")
    endif()
    target_link_libraries(dxgi PRIVATE dwmapi d3dcompiler d3d11)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(dxgi PRIVATE m)
endif()

set_target_properties(dxgi PROPERTIES PREFIX "")

# Alvo de teste executável local: Memória e Segurança
add_executable(test_memory_safety
    tests/test_memory_safety.cpp
    src/core/logger.cpp
    src/core/memory.cpp
    src/core/safety.cpp
    src/core/player.cpp
    src/core/savestate.cpp
)

target_include_directories(test_memory_safety PRIVATE
    include
    src
    vendor/minhook/include
)

if(NOT WIN32 AND EXISTS "/usr/include/wine/windows")
    target_include_directories(test_memory_safety PRIVATE /usr/include/wine/windows)
endif()

if(WIN32 OR MSVC)
    target_compile_definitions(test_memory_safety PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Alvo de teste executável local: Motor Lua e ModManager
add_executable(test_lua_engine
    tests/test_lua_engine.cpp
    src/core/logger.cpp
    src/core/memory.cpp
    src/core/safety.cpp
    src/core/player.cpp
    src/core/savestate.cpp
    src/ui/overlay.cpp
    src/script/lua_engine.cpp
    src/script/mod_manager.cpp
    ${IMGUI_CORE_SOURCES}
    ${LUA_SOURCES}
)

target_include_directories(test_lua_engine PRIVATE
    include
    src
    vendor/lua
    vendor/imgui
    vendor/imgui/backends
    vendor/minhook/include
)

if(NOT WIN32 AND EXISTS "/usr/include/wine/windows")
    target_include_directories(test_lua_engine PRIVATE /usr/include/wine/windows)
endif()

if(WIN32 OR MSVC)
    target_compile_definitions(test_lua_engine PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(test_lua_engine PRIVATE m)
endif()

# Alvo de teste executável local: Overlay ImGui e Notificações HUD
add_executable(test_overlay
    tests/test_overlay.cpp
    src/ui/overlay.cpp
    src/core/logger.cpp
    src/core/memory.cpp
    src/core/safety.cpp
    src/core/player.cpp
    src/core/savestate.cpp
    src/script/lua_engine.cpp
    src/script/mod_manager.cpp
    ${IMGUI_CORE_SOURCES}
    ${LUA_SOURCES}
)

target_include_directories(test_overlay PRIVATE
    include
    src
    vendor/lua
    vendor/imgui
    vendor/imgui/backends
    vendor/minhook/include
)

if(NOT WIN32 AND EXISTS "/usr/include/wine/windows")
    target_include_directories(test_overlay PRIVATE /usr/include/wine/windows)
endif()

if(WIN32 OR MSVC)
    target_compile_definitions(test_overlay PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(test_overlay PRIVATE m)
endif()

# Alvo de teste executável local: Estresse e Estabilidade Longa (Soak Testing - Fase 6)
add_executable(test_stability_soak
    tests/test_stability_soak.cpp
    src/core/logger.cpp
    src/core/memory.cpp
    src/core/safety.cpp
    src/core/player.cpp
    src/core/savestate.cpp
    src/ui/overlay.cpp
    src/script/lua_engine.cpp
    src/script/mod_manager.cpp
    ${IMGUI_CORE_SOURCES}
    ${LUA_SOURCES}
)

target_include_directories(test_stability_soak PRIVATE
    include
    src
    vendor/lua
    vendor/imgui
    vendor/imgui/backends
    vendor/minhook/include
)

if(NOT WIN32 AND EXISTS "/usr/include/wine/windows")
    target_include_directories(test_stability_soak PRIVATE /usr/include/wine/windows)
endif()

if(WIN32 OR MSVC)
    target_compile_definitions(test_stability_soak PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(test_stability_soak PRIVATE m)
endif()

# Alvo customizado para geração do pacote de release (Fase 6)
add_custom_target(package_release
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_release.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Gerando pacote compactado de release e hashes SHA-256 do DR2Hook"
)

