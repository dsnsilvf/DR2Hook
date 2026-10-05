# Passagem: limite de carros fantasma (2026-10-04, noite)

Branch/worktree: `worktree-ghost-limit-grok` em `.claude/worktrees/ghost-limit-grok` (commits WIP; **nada foi enviado ao origin e nada foi para a `feat/ghost-live`**). O checkout principal (`feat/ghost-live`) segue com tudo sem commit (F7, câmera livre, dano terminal etc.); o worktree contém um snapshot desse estado mais o trabalho abaixo.

## Resultado em uma linha
O jogo cria só 2 carros fantasma porque o complemento "até 5" usa **registros vazios**; trocando-os por cópia de um registro real (hook em `AddGhostEntry` `0x14057df00`) nascem 3, 4 e 5 veículos fantasma sem travar a carga. Só falta confirmar na tela que os 5 aparecem.

## O que foi feito e validado
| Teste (`dr2hook_ghost_cars.txt`) | Resultado |
|---|---|
| sem arquivo | 2 carros (comportamento do jogo) |
| zerar só `+0xb4` da 3ª entrada | carga trava para sempre (veículo sem dado da volta; `PollVehiclesReady` `0x1404b6160` exige `veículo+0x30/+0x38`) |
| 3 (troca registro vazio por cópia do último real = RecordingGhost tipo 2) | carga ok, **usuário viu os 3 carros** |
| 4 (copia o registro **tipo 0**, fantasma próprio) | carga ok, **usuário viu os 4** |
| 5 | carga ok; controladores, avaliação (4 posições distintas) e packer de `GhostCarValues` (6 objetos de render) corretos; **usuário viu só 2** (PENDENTE, ver abaixo) |

Ajustes necessários além do hook: o controlador do carro extra nasce com `+0x63 = 0` (6º arg de `AddGhostEntry`); o mod liga `+0x62`/`+0x63` de todo controlador cujo slot é cópia (`LinkAllCloneControllers`).

## PENDÊNCIA PRINCIPAL: com 5 só se viram 2 carros
Medido na corrida andando (23:01–23:02): 5 controladores ligados, 4 posições avaliadas distintas (8–30 m entre si), 6 objetos de render no packer, nenhuma exceção. Falta saber se o pixel aparece. Hipóteses: (1) cópias empilhadas na largada (saem 1 s, 2 s, 3 s... depois); (2) fora do campo de visão no cockpit; (3) fade de proximidade; (4) limite do renderizador (descritores de instância de transparência `GhostedTransparencyManager +0x290` com capacidade ~5 incl. jogador; NÃO medido). **Teste a fazer:** câmera livre (F9) com a corrida andando, contar carros; repetir com arquivo em 4 e em 5 e comparar. Se cair de 4 para 2 com 5, o limite é do renderizador: medir a capacidade da instância.
Obs.: o print da janela por Xlib fica congelado no menu de pausa; não serve para esse teste.

## Ainda não testado / riscos
- **Sair da especial** com 4–5 carros (desmontagem). A cópia de registro compartilha o ponteiro da volta; o subagente refutou o double free do destrutor de `+0x10`, mas não analisou a desmontagem dos slots.
- Vários Reiniciar seguidos com as cópias de dados.
- Passar de 5: ver `ghost-slots-analysis.md` §3–5 e `ghost-vectors-scan.md` (varredura dos destrutores dos vetores; **o subagente estava rodando quando esta nota foi escrita**; se o arquivo não existir, ela não terminou). Tetos: render 16 objetos (~15 fantasmas), corpos de física 24, vetores de 0x38 com capacidade 5 embutida, laço `5 - n` (n > 5 = ~4 bilhões de iterações). O código limita o arquivo de teste a 5.

## Estado do jogo/arquivos ao dormir
- Jogo aberto, core com diagnósticos carregado; `dr2hook_ghost_cars.txt` na pasta do jogo contém **5** (apague para voltar ao normal; o watchdog de threads de 25 s roda a cada carga enquanto o arquivo existir).
- Teclas: F7 = +1 cópia de dados (toast "Ghost data copy N; ghost cars drawn: M"); F7 foi tirado do checkpoint (F6 continua restaurando).
- Diagnóstico no log (`GhostLab[limite]`, `[espera]`, `[crash]`): lista da sessão, registros reais (tipo/draw/id), posições avaliadas, objetos de render no packer, estado da carga, VEH de crash.

## Arquivos de interesse
- Código: `src/core/ghost_lab.cpp` (hooks `AddGhostEntry`, `SpawnStageVehicles`, `EvaluateGhostState`, packer; `LinkAllCloneControllers`; `DumpThreads`; `CrashLogger`), `core_module.cpp` (F7), `ghost_lab.h`.
- Docs: `ghosts.md` §6.3, `loading_hangs.md`, `ghost-slots-analysis.md` (subagente Opus, 5 slots = 4 escolhidos + RecordingGhost; tipos 0/1/2), `investigations/grok/ghost-limit-prompt*.md` (prompts 1 e 2; a resposta do prompt 2 acertou a causa).
- Lição de método: análises (Grok, Gemini, subagentes e as minhas) erraram vários fatos; só valeu o que foi lido do código/memória ou testado no jogo.

## Próxima ideia (do usuário): ferramenta de análise de fantasmas
Ferramenta para inspecionar fantasmas (slots, registros, tipo, trajeto, tempos, quem é cada um) que depois servirá para **espectar jogadores** se o multiplayer for viável. Base já existente: `tools/dr2ghost.py` (lê GHST), `GhostLab` (slots, trajeto, diferença ao vivo), `ghost-slots-analysis.md` (tipo 0 próprio / 1 ranking / 2 RecordingGhost; registro de 0xb8 bytes). Pendências relacionadas: recifrar GHST para importar.
