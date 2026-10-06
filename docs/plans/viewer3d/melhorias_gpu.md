# viewer3d: melhorias de GPU (feitas e a fazer)

Nota das 3 rodadas de avaliação ([relatório final](../../reviews/viewer3d_relatorio_final.md)). Tudo foi medido no **Mesa llvmpipe** (renderização em software, sob Xvfb). Os números servem para comparar antes e depois, não como valor absoluto; nada foi medido na RTX 4050. A pista de estresse vem de `tools/viewer3d/tests/make_stress.py`: a sintética repetida numa grade de 8×8, com 9,7 M vértices, 16,9 M triângulos e 323 mil instâncias (tamanho da Polônia).

## Feito (rodada 3, commits `861254d` e `3c9a6e1`)

| O que | Onde | Resultado medido |
| --- | --- | --- |
| Corte por frustum do terreno, por malha (caixa de cada malha contra os 6 planos da câmera) | `render/terrain.cpp` `Terrain::draw` | Câmera padrão do estresse: 3,3 → 18,7 fps. Capturas da pista sintética iguais byte a byte, em 8 câmeras |
| `glMultiDrawElements` por material, em vez de um `glDrawElements` por malha | `render/terrain.cpp` | Menos chamadas e menos troca de estado; não foi medido à parte |
| Textura resolvida uma vez por parte (antes, uma busca por nome em todo quadro) | `render/terrain.cpp` (`tex_`) | ~10 mil buscas a menos por quadro no estresse |
| Raio do terreno a partir do alvo da câmera, opcional e desligado por padrão | `--terrain-dist`, slider **Terreno** | Com 3 km: rente ao chão 2,6 → 17,9 fps; panorâmica 1,8 → 5,7 fps. Corta malha a malha e deixa buracos |
| Envio do terreno malha por malha (`glBufferSubData`), sem a cópia intercalada da pista inteira | `Terrain::Terrain` | Pico de RAM na carga 1678 → 943 MB; envio 0,67 → 0,45 s |
| Arquivo e biblioteca de objetos liberados logo depois do envio; `malloc_trim` (glibc) | `app/track_view.cpp` | Junto com a linha acima. Abrir o estresse 3× pelo menu: pico 2198 → 1011 MB |
| Falta de memória de vídeo no envio estático vira erro de carga (a pista não abre pela metade) | `gl::Buffer::upload` | Só por leitura de código: não deu para provocar OOM aqui |
| Erros de GL por quadro avisam, aparecem na barra de status e não fecham o editor | `gl::warn` | Testado com erro injetado por `LD_PRELOAD` |
| A chave do corte das instâncias não inclui mais o zoom | `render/instances.cpp` `cull` | 10 cliques de roda: 0 recortes (antes, 1 por clique) |

## Feito (rodada 4, branch `feat/viewer3d-melhorias`)

Medido na RTX 4050 do dono (não mais no llvmpipe).

| O que | Onde | Resultado medido |
| --- | --- | --- |
| Texturas em segundo plano (2 threads, `libwebp` reduz ao decodificar), envio com orçamento de 16 MB por quadro, lado máximo 2048, limite de VRAM com descarte das menos usadas | `render/texture.cpp` `TextureCache`; `--tex-mb`, `--tex-max-side`, `--tex-threads`, `--wait-textures` | Montalegre, 3 texturas de 8191×6007: a carga travava 3,9 s, agora abre na hora. Com `--tex-mb 16` e `64` descarta 345 e 307 e não recarrega em laço. Commit `e31be71` |
| Grade espacial de 64 m nas instâncias; corte e picking percorrem só as células perto do alvo; arrastar e digitar reenviam só a instância (`touch`) | `core/grid.cpp`, `render/instances.cpp`; `--walk` | Pista de estresse (927 mil instâncias), andando: 112,6 → 167,1 fps; corte completo 1,1 ms. Commit `db185b3` |
| Picking que respeita o terreno: o morro na frente tapa o que está atrás | `render/terrain_probe.cpp` `TerrainProbe`; `TrackView::pick_ray` | Sonda de raio na GPU (FBO R32F, mistura `GL_MIN`, só as malhas no frustum). Contra Möller–Trumbore em numpy sobre os 558 mil triângulos da Montalegre: 300 raios, 0 divergências, pior erro 1,5 cm |
| **Pôr no chão** (botão, Editar, tecla **T**) e **Grudar no chão** no arraste | `TrackView::settle_selected`, `follow_ground` | Em 600 instâncias o pior erro de altura é 0,15 cm. Reenvio parcial × corte completo: 0 pixels de diferença em 5 cenários, inclusive mudando de célula |
| Decalques do terreno com transparência de verdade (`mnt_decal_*`) misturados por alfa, sem escrever profundidade, com deslocamento de polígono | `Terrain::draw` (2 passadas), `TextureCache::translucent` | Acaba o z-fighting e as manchas cinza nos rastros de pneu. O alfa das outras texturas do terreno (asfalto, bordas, grama) é mapa de brilho, não transparência: continuam opacas |
| Oclusão ambiente do chão (`o|ground_ao*`) multiplicada sobre o terreno, não desenhada opaca | `InstanceRenderer::draw` (passada final, `GL_DST_COLOR`) | Some o quadrado branco embaixo de cada carro e prédio. Custo medido: ~0,4 ms por quadro na Montalegre (335 → 295 fps sem vsync) |

Ferramentas de teste que ficaram: `--probe-rays`, `--ground-check`, `--settle-list [--settle-redo]`, `--touch-test`, `--hide` e os scripts `tests/probe_check.py` e `tests/ground_check.py` (ver o README).

## A fazer, em ordem de prioridade

### 1. Texturas: compressão (o resto do item foi feito, ver acima)

**Hoje:** a textura vai para a GPU em RGBA8, com mipmaps gerados no driver, limitada a 2048 de lado e a 1536 MB estimados.

**Estimativa (HYPOTHESIS):** 300 texturas de 2048² dariam ~6,4 GB em RGBA8 com mipmaps, mais que os 6 GB da 4050. Falta saber o tamanho das texturas de uma pista real.

**Proposta:**
1. Usar compressão BC: BC1 sem alfa, BC3 com alfa, 4 a 8 vezes menos VRAM. O caminho mais curto é pedir ao driver `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` no `glTexImage2D`; a compressão sai lenta e de qualidade média. O melhor caminho é o exportador Python guardar o **DDS original do jogo**, que já vem em DXT, e o viewer enviá-lo direto com `glCompressedTexImage2D`, sem decodificar nada.

**Teste:** pista com texturas enormes, sem travar o primeiro quadro; contador de VRAM antes e depois; capturas comparadas com tolerância por causa da compressão.

### 2. Instâncias: o que falta da grade espacial

1. Corte por frustum das células, além do raio.
2. Mais adiante: LOD por distância, com árvores longe como impostores (um quad com a textura).

### 3. Terreno: menos memória e menos vértices

1. **Índices de 16 bits por malha** com `glDrawElementsBaseVertex`, quando a malha tem até 65 535 vértices (quase todas). Corta ~metade do IBO: ~200 → ~100 MB no estresse.
2. **Raio automático:** ligar um raio padrão (por exemplo 3 km) quando o terreno passar de ~5 M vértices, com aviso. Hoje o raio é manual e vem desligado.
3. **LOD do terreno:** uma versão simplificada de cada bloco, gerada no exportador Python, para longe da câmera. Isso tira os buracos do raio e segura a panorâmica.
4. **Opcional:** `glMultiDrawElementsIndirect` (GL 4.3) quando existir, mantendo o caminho 3.3.

### 4. Medir a GPU de verdade dentro do editor

1. **VRAM livre e usada** pelas extensões `GL_NVX_gpu_memory_info` (NVIDIA) e `GL_ATI_meminfo` (AMD), na barra de status quando existirem.
2. **Tempo de GPU por passada** (terreno, objetos, linhas, painéis) com `glQueryCounter`/`GL_TIME_ELAPSED`, numa janela "Desempenho". Hoje só há o FPS.
3. **`glDebugMessageCallback` (`GL_KHR_debug`)** num build de depuração, para ver a mensagem do driver em vez de só o código do erro.

### 5. Outras

- **MSAA** opcional, para as bordas do alambrado e das barreiras.
- **Recorte por alfa** com *alpha-to-coverage* quando houver MSAA: hoje a folhagem tende a sumir ao longe por causa dos mipmaps.
- **Precisão de profundidade:** com near 0,3 e far 20 km, a precisão a 3 km fica perto de 1,8 m (os mesmos valores do web). O caminho é profundidade reversa com `glClipControl` (GL 4.5) quando existir.
- **Perda de contexto** (driver reiniciado): hoje não é tratada.

## Checklist para a RTX 4050 (na máquina do dono)

```bash
cmake -S tools/viewer3d -B build/viewer3d -G Ninja && cmake --build build/viewer3d
python3 tools/viewer3d/tests/make_stress.py build/uiview/tracks/portugal__montalegre_rallycross build/stress/tracks/montalegre_x64
export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia
for cam in "" "--camera 0.8,0.08,30,0,101,0" "--camera 0.8,0.9,9000,0,100,0"; do
  ./build/viewer3d/viewer3d --track build/uiview/tracks/portugal__montalegre_rallycross --fresh --vsync 0 --frames 300 $cam
done
```

Anotar para cada câmera, na Montalegre e na pista de estresse:

| Câmera | FPS | Tempos de carga e envio (stderr) | VRAM (`nvidia-smi`) | RAM (pico) |
| --- | --- | --- | --- | --- |
| Montalegre, padrão | 323 | leitura 0,006 s, envio 0,009 s | 671 MB | 344 MB |
| Montalegre, rente ao chão | 433 | idem | 621 MB | 351 MB |
| Montalegre, panorâmica | 351 | idem | 674 MB | 352 MB |
| Estresse (27,4 M vértices, 35,7 M triângulos), padrão | 159 | leitura 0,68 s, envio 0,73 s | 1697 MB | 2270 MB |
| Estresse, rente ao chão | 164 | leitura 0,51 s, envio 0,71 s | 1696 MB | 2270 MB |
| Estresse, panorâmica | 154 | leitura 0,53 s, envio 0,69 s | 1698 MB | 2270 MB |

Medido na RTX 4050 do dono em 2026-10-06, `--vsync 0 --frames 300 --wait-textures`, commit `3435fb2`; a VRAM é a leitura de `GL_NVX_gpu_memory_info` do próprio viewer, com 42 MB em uso antes de abrir. Pior quadro: 7 a 22 ms na Montalegre, 17 a 21 ms no estresse. Texturas da Montalegre: 335 a 376 na GPU, 217 a 267 MB estimados de 1536, 0 descartadas, ~0,5 s de decodificação nas threads. As três câmeras do estresse vêm da checklist e olham para o mesmo ponto fixo `(0, 100, 0)`; o número parecido nas três indica que o corte por frustum e a grade seguram a pista de 27 M vértices, mas elas não varrem a pista toda.

Com esses números, decidir:
- se o raio automático (item 3.2) é necessário;
- o quanto a compressão de texturas (item 1) é urgente.

O alvo do plano era **≥ 60 FPS** na Montalegre. Para a pista de estresse ainda não há alvo.
