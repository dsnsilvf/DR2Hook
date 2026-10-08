# Dear ImGui (cópia para o viewer3d)

- Versão: **1.91.9b, ramo docking**, tag `v1.91.9b-docking` de <https://github.com/ocornut/imgui> (commit `4806a1924ff6181180bf5e4b8b79ab4394118875`). O ramo docking é o master mais os painéis encaixáveis (`DockSpace`, `DockBuilder*`) que o editor usa.
- Licença: MIT (`LICENSE.txt`).
- Arquivos copiados sem mudança:
  - o núcleo (`imgui*.cpp/h`, `imstb_*.h`, `imconfig.h`);
  - os backends `imgui_impl_sdl3` e `imgui_impl_opengl3`;
  - `misc/freetype/imgui_freetype.cpp/h`, que o CMake só compila quando acha o FreeType.

É separada de `vendor/imgui` (1.90.4, backends Win32/DX11), que o hook usa dentro do jogo. O backend SDL3 da 1.90.4 é anterior ao SDL 3.2 e não compila com ele, e atualizar a cópia do hook está fora do escopo do viewer.

Para atualizar: copie os mesmos arquivos de outra tag `-docking` e atualize esta nota.
