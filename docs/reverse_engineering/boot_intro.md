# Abertura do jogo: logo, avisos e título

O que o DiRT Rally 2.0 mostra entre a janela abrir e o título "Pressione Start ou Enter", e o que o core já
corta. Medido em 2026-10-07: abertura normal pela Steam, Proton, cache quente, um print por segundo.

## 1. Sequência

| Tela | Duração | O que é |
|---|---|---|
| Splash do DR2 | ~2,3 s | janela própria de 512x512 com o logo grande, ~6 s depois de abrir o jogo (ver §4) |
| `splash_image` | < 0,4 s | logo pequeno desenhado na janela do jogo de 1920x1080, assim que ela abre |
| Logo da Codemasters | 7 s | vídeo `video/studio_logo.bk2` (Bink 2, 1920x1080, 209 quadros a 29,97 fps) |
| Avisos legais | ~5,2 s | texto e logos das marcas; dura o mesmo com ou sem o vídeo, parece temporizada |
| Título | até uma tecla | "Pressione Start ou Enter"; depois de ~30 s parado, toca `video/attract.bk2` (68 s) |

| Abertura | Da janela até o título |
|---|---|
| Original | 13,6 s |
| Com o IntroSkip | 6,3 s |

O caminho do AutoStage (benchmark, carga rápida do editor) não abre vídeo nenhum: vai direto para a especial.

## 2. Como o jogo toca os vídeos

- O exe importa do `bink2w64.dll` o `BinkOpen`, o `BinkSetFileOffset`, o `BinkDoFrameAsyncMulti`, o
  `BinkNextFrame`, o `BinkClose` e outros.
- O `BinkOpen` é chamado em dois lugares:
  - `0x140cae5b0`, o abridor genérico: com o bit 26 das flags chama o `BinkOpen`; sem ele, usa o ponteiro
    `[exe+0x15b9510]`;
  - `0x140ca72b0`: chama o `BinkSetFileOffset` e depois o `BinkOpen` com `flags | 0x20`.
- Na abertura, o vídeo chega com o caminho completo (`S:\steamapps\common\DiRT Rally 2.0\video\studio_logo.bk2`)
  e as flags `0x460`. O attract usa as mesmas flags.
- Nomes no exe: `video/%s.bk2` (usado em `0x287180`, `0x2b5fb0`, `0x310a50`, `0x3181e0`, `0x338630`; telas
  com `play_video` / `show_play_video`), `video/dummy.bk2` (em `0x310a50`; o arquivo não existe na pasta) e
  `StateAttractVideo`.
- O `studio_logo` não aparece como string no exe; o nome vem dos dados.

## 3. IntroSkip (core)

`src/core/intro_skip.cpp` faz um hook no `BinkOpen`:

- quando o caminho contém `studio_logo`, devolve `NULL`, como se o arquivo faltasse. O jogo pula para os
  avisos legais sem erro nem crash;
- loga cada vídeo aberto: `IntroSkip: video <caminho> (flags 0x…) -> <HBINK>, +<ms desde o core>`.

Vem ligado por padrão. Para desligar, crie `dr2hook_intro.ini` na pasta do jogo com `pular_logo=0`. O hook sai no
`Shutdown` do core (F8).

## 4. SplashSkip (dxgi)

O splash de 512x512 é criado em `0x1403a5470`:

- `CreateWindowExW` com exstyle `0x80080` (`WS_EX_LAYERED | WS_EX_TOOLWINDOW`) e style `0xcf0000`, centrada;
- o hwnd fica em `[obj+0x100]`, seguido de `ShowWindow(SW_SHOW)` e `UpdateWindow`;
- uma thread a pinta com `UpdateLayeredWindow` a cada 20 ms (`0x1403ccc2e`, `Sleep(20)`) até a janela do
  jogo abrir. O `DestroyWindow` fica em `0x1403a7b37`.

Esse `ShowWindow` vem antes do core carregar. Por isso o hook fica na dxgi.dll (`src/core/splash_skip.cpp`),
instalado no `DLL_PROCESS_ATTACH`. Ele troca no IAT do exe os slots do `ShowWindow` (RVA `0x109fad8`) e do
`UpdateLayeredWindow` (`0x109fae0`). Uma janela layered de 512x512 nunca é mostrada e as pinturas dela viram
no-op.

- Log: `SplashSkip: ativo.` e `SplashSkip: splash do logo escondido no ShowWindow (janela …)`.
- Validado em 2026-10-08: a janela nunca é mapeada.
- O tempo da abertura não muda: no lugar do logo, fica a área de trabalho por ~2 s.
- Para desligar, use `pular_splash=0` no `dr2hook_intro.ini`.

## 5. O que falta

- **Avisos legais (~5,2 s):** achar a tela e o temporizador (o `StateLegal…` / frontend em `game_1.dat`, ver
  [UI Data](ui_data.md)). Como a duração não muda quando o vídeo some, deve ser só tempo fixo.
- **Título:** ir direto ao menu principal, sem esperar a tecla.
- **`splash_image` (< 0,4 s):** desenhado por `0x1404b3f70`, chamado de `0x1404b1c07` na função de quadro, sob as flags `[r+0x1e0]`/`[r+0x1e1]`; não foi cortado.
