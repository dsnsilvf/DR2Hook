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

## A fazer, em ordem de prioridade

### 1. Texturas: carga em segundo plano, compressão e limite de VRAM (maior risco)

**Hoje:**
- o WebP é decodificado na thread do quadro, na primeira vez que um material aparece;
- a textura vai para a GPU em RGBA8, com mipmaps gerados no driver;
- não há limite de tamanho nem contagem de memória.

**Efeito medido:** uma textura de 8191×6007 travou o primeiro quadro por 2,5 s.

**Estimativa (HYPOTHESIS):** 300 texturas de 2048² dariam ~6,4 GB em RGBA8 com mipmaps, mais que os 6 GB da 4050. Falta saber o tamanho das texturas de uma pista real.

**Proposta:**
1. Decodificar o WebP numa thread de trabalho e enviar no quadro com orçamento (por exemplo, até 8 MB por quadro). Enquanto a textura não chega, o material fica na cor fixa, como já acontece quando ela falha.
2. Usar compressão BC: BC1 sem alfa, BC3 com alfa, 4 a 8 vezes menos VRAM. O caminho mais curto é pedir ao driver `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` no `glTexImage2D`; a compressão sai lenta e de qualidade média. O melhor caminho é o exportador Python guardar o **DDS original do jogo**, que já vem em DXT, e o viewer enviá-lo direto com `glCompressedTexImage2D`, sem decodificar nada.
3. Limitar o lado maior a 2048 (reduzir na carga) e somar os bytes enviados. O Inspector já mostra "MB RGBA"; passaria a mostrar a VRAM estimada.

**Teste:** pista com texturas enormes, sem travar o primeiro quadro; contador de VRAM antes e depois; capturas comparadas com tolerância por causa da compressão.

### 2. Instâncias: corte incremental e envio só do que mudou

**Hoje:**
- a cada passo de 8 m do alvo (andando) e a cada movimento de um arraste, `cull` varre todas as instâncias (323 mil no estresse);
- refaz o buffer de cada tipo com `glBufferData`;
- medido: ~4,5 ms por recorte e 104 recortes em 4 s segurando W.

**Proposta:**
1. Grade espacial (células de 64 m) montada na carga e no `regroup`. O corte percorre só as células dentro do raio.
2. Durante um arraste, atualizar só a instância arrastada com `glBufferSubData` no lugar dela, em vez de recortar tudo.
3. Corte por frustum das células, além do raio.
4. Mais adiante: LOD por distância, com árvores longe como impostores (um quad com a textura).

**Teste:** contador de recortes e de tempo, como na instrumentação do avaliador (`r3eval/instrumentacao.diff`); 0 recortes completos durante um arraste.

### 3. Picking que respeita o terreno

**Hoje:** o raio testa só as caixas dos objetos. Dá para selecionar um objeto atrás de um morro (confirmado por teste).

**Proposta:** no clique, ler a profundidade do pixel com `glReadPixels(GL_DEPTH_COMPONENT)` e descartar os objetos atrás dela. Alternativa sem GPU: interseção do raio com os triângulos do terreno, usando as caixas das malhas como aceleração.

**Teste:** clicar numa árvore escondida atrás do relevo não seleciona nada.

### 4. Terreno: menos memória e menos vértices

1. **Índices de 16 bits por malha** com `glDrawElementsBaseVertex`, quando a malha tem até 65 535 vértices (quase todas). Corta ~metade do IBO: ~200 → ~100 MB no estresse.
2. **Raio automático:** ligar um raio padrão (por exemplo 3 km) quando o terreno passar de ~5 M vértices, com aviso. Hoje o raio é manual e vem desligado.
3. **LOD do terreno:** uma versão simplificada de cada bloco, gerada no exportador Python, para longe da câmera. Isso tira os buracos do raio e segura a panorâmica.
4. **Opcional:** `glMultiDrawElementsIndirect` (GL 4.3) quando existir, mantendo o caminho 3.3.

### 5. Medir a GPU de verdade dentro do editor

1. **VRAM livre e usada** pelas extensões `GL_NVX_gpu_memory_info` (NVIDIA) e `GL_ATI_meminfo` (AMD), na barra de status quando existirem.
2. **Tempo de GPU por passada** (terreno, objetos, linhas, painéis) com `glQueryCounter`/`GL_TIME_ELAPSED`, numa janela "Desempenho". Hoje só há o FPS.
3. **`glDebugMessageCallback` (`GL_KHR_debug`)** num build de depuração, para ver a mensagem do driver em vez de só o código do erro.

### 6. Outras

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

Com esses números, decidir:
- se o raio automático (item 4.2) é necessário;
- o quanto a compressão de texturas (item 1) é urgente.

O alvo do plano era **≥ 60 FPS** na Montalegre. Para a pista de estresse ainda não há alvo.
