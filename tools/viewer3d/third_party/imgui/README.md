# Dear ImGui (cópia para o viewer3d)

- Versão: **1.91.9b**, tag `v1.91.9b` de <https://github.com/ocornut/imgui> (commit `f5befd2d29e66809cd1110a152e375a7f1981f06`).
- Licença: MIT (`LICENSE.txt`).
- Arquivos copiados sem mudança: o núcleo (`imgui*.cpp/h`, `imstb_*.h`, `imconfig.h`) e os backends `imgui_impl_sdl3` e `imgui_impl_opengl3`.

É separada de `vendor/imgui` (1.90.4, backends Win32/DX11), que o hook usa dentro do jogo: o backend SDL3 da 1.90.4 é anterior ao SDL 3.2 e não compila com ele, e atualizar a cópia do hook está fora do escopo do viewer.

Para atualizar: copie os mesmos arquivos de outra tag e atualize esta nota.
