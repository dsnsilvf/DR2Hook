cmake_minimum_required(VERSION 3.20)
project(dr2hook LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32 AND EXISTS "/usr/include/wine/wine/windows/windows.h")
    set(DR2HOOK_WINE_WINDOWS_INCLUDE "/usr/include/wine/wine/windows")
elseif(NOT WIN32 AND EXISTS "/usr/include/wine/windows")
    set(DR2HOOK_WINE_WINDOWS_INCLUDE "/usr/include/wine/windows")
endif()

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

# Proxy residente. Os hooks ficam aqui; o resto mora em dr2hook_core.dll.
add_library(dxgi SHARED
    src/proxy/dxgi_proxy.cpp
    src/core/main.cpp
    src/core/host.cpp
    src/core/hooks.cpp
    src/core/logger.cpp
    src/core/auto_stage.cpp
    src/core/load_trace.cpp
    src/core/race_events.cpp
    src/core/pssg_patch.cpp
    src/core/pause_menu.cpp
    src/core/native_screen.cpp
    src/core/ui_data.cpp
    src/core/bxml.cpp
    src/core/ui_patch.cpp
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde32.c
    vendor/minhook/src/hde/hde64.c
)

# Módulo recarregável em runtime (F8). A dxgi.dll carrega uma cópia deste arquivo.
add_library(dr2hook_core SHARED
    src/core/core_module.cpp
    src/core/logger.cpp
    src/core/memory.cpp
    src/core/safety.cpp
    src/core/player.cpp
    src/core/savestate.cpp
    src/core/physics_tick_harness.cpp
    src/core/cutscene_probe.cpp
    src/core/ghost_lab.cpp
    src/core/net_dialog.cpp
    src/ui/overlay.cpp
    src/script/lua_engine.cpp
    src/script/mod_manager.cpp
    src/script/mod_menu.cpp
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_BACKEND_SOURCES}
    ${LUA_SOURCES}
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde32.c
    vendor/minhook/src/hde/hde64.c
)

foreach(dr2_target dxgi dr2hook_core)
    target_include_directories(${dr2_target} PRIVATE
        include
        src
        vendor/lua
        vendor/imgui
        vendor/imgui/backends
        vendor/minhook/include
    )

    if(NOT WIN32 AND DR2HOOK_WINE_WINDOWS_INCLUDE)
        target_include_directories(${dr2_target} PRIVATE ${DR2HOOK_WINE_WINDOWS_INCLUDE})
        target_compile_definitions(${dr2_target} PRIVATE
            WINE_UNICODE_NATIVE
            _DPI_AWARENESS_CONTEXTS_
        )
    elseif(NOT WIN32 AND EXISTS "/usr/include/wine/windows")
        target_include_directories(${dr2_target} PRIVATE /usr/include/wine/windows)
        target_compile_definitions(${dr2_target} PRIVATE
            WINE_UNICODE_NATIVE
            _DPI_AWARENESS_CONTEXTS_
        )
    endif()

    if(WIN32 OR MSVC)
        target_compile_definitions(${dr2_target} PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            DR2HOOK_USE_BACKENDS
        )
        target_compile_options(${dr2_target} PRIVATE "-Dlocaltime_r(t,b)=(localtime_s((b),(t)),(b))")
        if(MINGW)
            target_link_options(${dr2_target} PRIVATE
                "-static"
                "-static-libgcc"
                "-static-libstdc++"
            )
        endif()
        target_link_libraries(${dr2_target} PRIVATE dwmapi d3dcompiler d3d11 ws2_32)
    endif()

    if(UNIX AND NOT APPLE)
        target_link_libraries(${dr2_target} PRIVATE m)
    endif()

    set_target_properties(${dr2_target} PROPERTIES PREFIX "")
endforeach()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    enable_language(ASM)
    target_sources(dr2hook_core PRIVATE src/core/physics_harness_detour_x64.S)
    set_source_files_properties(src/core/physics_harness_detour_x64.S
        PROPERTIES LANGUAGE ASM)
endif()

target_compile_definitions(dr2hook_core PRIVATE DR2HOOK_CORE_MODULE)

if((WIN32 OR MSVC) AND MINGW)
    target_link_options(dr2hook_core PRIVATE
        "-Wl,--defsym,_ZN7dr2hook30ImGui_ImplWin32_WndProcHandlerEP6HWND__jyx=_Z30ImGui_ImplWin32_WndProcHandlerP6HWND__jyx"
    )
endif()

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

if(NOT WIN32 AND DR2HOOK_WINE_WINDOWS_INCLUDE)
    target_include_directories(test_memory_safety PRIVATE ${DR2HOOK_WINE_WINDOWS_INCLUDE})
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
    src/core/cutscene_probe.cpp
    src/core/ghost_lab.cpp
    src/script/mod_manager.cpp
    src/script/mod_menu.cpp
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

if(NOT WIN32 AND DR2HOOK_WINE_WINDOWS_INCLUDE)
    target_include_directories(test_lua_engine PRIVATE ${DR2HOOK_WINE_WINDOWS_INCLUDE})
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
    src/core/cutscene_probe.cpp
    src/core/ghost_lab.cpp
    src/script/mod_manager.cpp
    src/script/mod_menu.cpp
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

if(NOT WIN32 AND DR2HOOK_WINE_WINDOWS_INCLUDE)
    target_include_directories(test_overlay PRIVATE ${DR2HOOK_WINE_WINDOWS_INCLUDE})
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
    src/core/cutscene_probe.cpp
    src/core/ghost_lab.cpp
    src/script/mod_manager.cpp
    src/script/mod_menu.cpp
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

if(NOT WIN32 AND DR2HOOK_WINE_WINDOWS_INCLUDE)
    target_include_directories(test_stability_soak PRIVATE ${DR2HOOK_WINE_WINDOWS_INCLUDE})
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

# Alvo de teste executável local: XML binário e patch dos documentos de UI
add_executable(test_ui_patch
    tests/test_ui_patch.cpp
    src/core/bxml.cpp
    src/core/ui_patch.cpp
)

target_include_directories(test_ui_patch PRIVATE include)

# Alvo de teste executável local: opções de mod da tela nativa (sem Lua)
add_executable(test_mod_menu
    tests/test_mod_menu.cpp
    src/script/mod_menu.cpp
)

target_include_directories(test_mod_menu PRIVATE include)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows" OR CMAKE_CROSSCOMPILING)
    enable_language(ASM)
    set(PHYSICS_DETOUR_WINE_SOURCES
        tests/test_physics_harness_detour_wine.cpp
        tests/physics_detour_wine_ops_stub.cpp
        tests/physics_detour_wine_target_x64.S
        tests/physics_detour_wine_caller_x64.S
        tests/physics_detour_wine_entry_x64.S
        src/core/physics_harness_detour_x64.S
        vendor/minhook/src/buffer.c
        vendor/minhook/src/hook.c
        vendor/minhook/src/trampoline.c
        vendor/minhook/src/hde/hde64.c
    )
    add_executable(test_physics_harness_detour_wine ${PHYSICS_DETOUR_WINE_SOURCES})
    target_include_directories(test_physics_harness_detour_wine PRIVATE
        include
        vendor/minhook/include
    )
    target_compile_definitions(test_physics_harness_detour_wine PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
    if(MINGW OR CMAKE_CROSSCOMPILING)
        target_link_options(test_physics_harness_detour_wine PRIVATE
            "-static"
            "-static-libgcc"
            "-static-libstdc++"
        )
    endif()
endif()

add_executable(test_physics_tick_harness
    tests/test_physics_tick_harness.cpp
    src/core/logger.cpp
    src/core/physics_tick_harness.cpp
    src/core/safety.cpp
    src/core/memory.cpp
)

target_include_directories(test_physics_tick_harness PRIVATE
    include
    src
    vendor/minhook/include
)

if(NOT WIN32 AND DR2HOOK_WINE_WINDOWS_INCLUDE)
    target_include_directories(test_physics_tick_harness PRIVATE ${DR2HOOK_WINE_WINDOWS_INCLUDE})
endif()

if(WIN32 OR MSVC)
    target_compile_definitions(test_physics_tick_harness PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        DR2HOOK_PHYSICS_HARNESS_TESTING
    )
else()
    target_compile_definitions(test_physics_tick_harness PRIVATE
        DR2HOOK_PHYSICS_HARNESS_NO_HOOKS
        DR2HOOK_PHYSICS_HARNESS_TESTING
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(test_physics_tick_harness PRIVATE m)
endif()

add_executable(test_auto_stage
    tests/test_auto_stage.cpp
    src/core/auto_stage.cpp
    src/core/logger.cpp
)

target_include_directories(test_auto_stage PRIVATE
    include
    src
    vendor/minhook/include
)

if(NOT WIN32 AND DR2HOOK_WINE_WINDOWS_INCLUDE)
    target_include_directories(test_auto_stage PRIVATE ${DR2HOOK_WINE_WINDOWS_INCLUDE})
endif()

if(WIN32 OR MSVC)
    target_compile_definitions(test_auto_stage PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Alvo customizado para geração do pacote de release (Fase 6)
add_custom_target(package_release
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_release.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Gerando pacote compactado de release e hashes SHA-256 do DR2Hook"
)

