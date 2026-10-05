# Etapa 5 — Texturas

Antes: [README do viewer 3D](README.md) e [formatos.md](formatos.md). Anterior: [etapa 4](etapa_4_terreno.md) cumprida.

## Objetivo

O terreno com "cara de pista": cada malha usa a textura de cor do seu material, decodificada de WebP e enviada à GPU com mipmaps.

## Arquivos

- `src/render/texture.{hpp,cpp}` (novos): decodificação WebP e cache de texturas GL.
- `src/render/terrain.cpp`: shader com `sampler2D`, desenho agrupado por material.
- `src/core/track.{hpp,cpp}`: expõe `materials` (`std::map<std::string, std::string>`).
- `CMakeLists.txt`: `dr2render` liga `libwebp` (`pkg_check_modules(WEBP REQUIRED libwebp)`).

## Implementação

- **Mapa**: `track.json → materials` associa a chave de material (`g|S1edg_03!1`, `o|...`, `t|...`) a `tex/<arquivo>.webp`, relativo à pasta da pista. Material sem entrada = sem textura: use a cor fixa da etapa 4.
- **Decodificação**: `WebPDecodeRGBA(dados, tamanho, &w, &h)` (libwebp 1.6 está instalada); libere com `WebPFree`. O arquivo lê-se com `std::ifstream` (nada de `mmap` ainda). Todas as texturas são RGBA8; as de cor sem alfa também (o web faz o mesmo).
- **Cache**: `std::unordered_map<std::string /*arquivo*/, GLuint>`; o mesmo arquivo serve a vários materiais, e na Montalegre 185 materiais de terreno usam só 41 arquivos. Envie com `glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, ...)` e `glGenerateMipmap`. Filtro `GL_LINEAR_MIPMAP_LINEAR`/`GL_LINEAR`; `GL_REPEAT` nos dois eixos (o web só repete em potência de dois por limitação do WebGL 1; em OpenGL 3.3 repetir sempre é o certo e **é a única diferença deliberada** em relação ao web). Se existir `GL_EXT_texture_filter_anisotropic`, use 8×.
- **Carga**: decodifique **só o que o terreno usa** nesta etapa (41 arquivos, ~37 MB em RGBA). A Montalegre inteira tem 381 arquivos / ~204 MB em RGBA, mas os de objetos são da etapa 6. Decodificar no laço principal é aceitável aqui (registre o tempo); threads ficam para depois.
- **Desenho**: ordene as malhas do terreno por textura (o `first_index` por malha permite reordenar os intervalos) e troque `glBindTexture` só quando a textura muda. Shader: `vec4 tx = texture(uTex, vUv)`; `base = tx.rgb`; materiais com prefixo `g` **não** descartam por alfa (o terreno é opaco); mantenha a luz por derivadas da etapa 4.
- **`v` de UV**: o web carrega a imagem com `UNPACK_FLIP_Y_WEBGL = false`, ou seja, a **linha 0 da imagem é a primeira linha da textura** e as UVs do arquivo já assumem isso. Faça igual (não inverta nada no C++). Se o resultado sair de cabeça para baixo, o erro está na ordem das linhas ao subir, não nas UVs.

## Critério de pronto

```bash
cmake --build build/viewer3d
./build/viewer3d/core_tests --track build/uiview/tracks/portugal__montalegre_rallycross
./build/viewer3d/viewer3d --track build/uiview/tracks/portugal__montalegre_rallycross --frames 60 --screenshot build/viewer3d/e5.ppm
```

- `core_tests` ganha: o `materials` do `track.json` tem 820 entradas e todo arquivo citado existe em `tex/`.
- Compare `e5.ppm` com uma captura do viewer web na mesma câmera (a aba Pistas, mesma pista, camada "Objetos" desligada): o terreno deve ter as mesmas texturas (asfalto/terra/grama nos mesmos lugares). Anexe as duas imagens ao commit **não** (binários); descreva a comparação no corpo.
- Sem tela preta e sem terreno "todo cinza" (cor fixa em todos os blocos = mapa de materiais não aplicado).
- `glGetError` limpo; nenhum vazamento óbvio (`texturas criadas == arquivos distintos usados`, impresso no `stderr`).

## Riscos e bloqueios

- **Material ausente** no mapa (a exportação registra `found 381/382`): cor fixa, sem erro.
- **Textura de blocos "batched_track"**: o terreno de estrada (`batched_track.fx`) não declara textura e fica na cor de terra fixa; o web é igual (limitação conhecida, veja `docs/tools/uiview.md`).
- **Terreno misturado** (`terrain_wsm_*`): só a primeira textura difusa é usada; igual ao web.
- **Memória de textura**: ~37 MB só do terreno; tudo cabe sem problema na RTX 4050 (6 GB). Não otimize antes de medir.

## Fora do escopo

Alfa dos objetos (etapa 6), compressão de textura na GPU (BCn), `mmap`, carga em threads.
