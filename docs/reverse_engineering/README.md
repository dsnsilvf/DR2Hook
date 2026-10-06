# Engenharia reversa da EGO Engine: DiRT Rally 2.0

Reorganização do antigo `docs/REVERSE_ENGINEERING.md` (removido; o índice geral agora é [`docs/README.md`](../README.md)). O texto técnico foi repartido por subsistema. Nenhuma descoberta, offset, endereço, fórmula ou classificação foi acrescentada a partir de fora desse material.

A conclusão aceita hoje e a hipótese que ela substituiu ficam as duas no documento do subsistema. Quando o arquivo original usa `CONFIRMADO`, `PROVÁVEL`, `INCONCLUSIVO`, `REJEITADO`, `UNKNOWN`, `CONFIRMED` ou `INCONCLUSIVE`, o documento novo repete essa palavra.

Nome de string no executável não é variável localizada.

## Vehicle

- [PhysicsRig](physics_rig.md) — referência estrutural: cadeia de ponteiros, stride, blocos, campos compartilhados
- [Vehicle Transform](vehicle_transform.md) — posição, quatérnion, base Right/Up/Forward, âncora visual
- [Vehicle Physics](vehicle_physics.md) — velocidades linear e angular, e os campos `T` / `V` copiados para elas
- [Vehicle Setup](vehicle_setup.md) — bloco decodificado de setup, ponto do eixo, bitola e entre-eixos
- [Wheels](wheels.md) — vetores da roda, ordem dos blocos, quociente e tabela 4×4
- [Suspension](suspension.md) — escalar `+0x1504`, integrador e o termo somado em `+0x1660`
- [Tyres](tyres.md) — byte de estado do pneu e da roda em `+0x2cf0`
- [Damage](damage.md) — canais de dano nomeados no executável e hipóteses de dano rejeitadas
- [Engine](engine.md) — virabrequim, marcha lenta, potência máxima e corte
- [Gearbox](gearbox.md) — quantidade de marchas e marcha engatada

## Other systems

- [Telemetry](telemetry.md) — pacote UDP nativo e o que o parser não identifica
- [Network Guard](network_guard.md) — isolamento Winsock do mod loader
- [Executable](executable.md) — binário, seções PE e compilador
- [Menu](menu.md) — `StatePauseScreen`, chave `pause_menu` e o item DR2 Hook
- [UI Data](ui_data.md) — telas, estados e fluxo em `game_1.dat` (NeFS e XML binário), e a ferramenta `tools/egodata`
- [UI Tabs](ui_tabs.md) — `SBTabGroup`, estados com abas, lista com rolagem, combos e a API do data store
- [UI Limits](ui_limits.md) — limites e capacidades da UI nativa: objetos, posições, behaviours, hot buttons, texto, diálogos e testes pendentes
- [UI escondida](ui_hidden.md) — telas, textos e opções que existem nos dados ou no executável e não aparecem nos menus
- [Renderização da UI](ui_render.md) — como o frontend e o HUD desenham, e por onde um mod pode desenhar de forma nativa
- [Câmera](camera.md) — dono, tick, olho em `+0x240`, base cima/direita/frente e a câmera livre do core (F9)
- [Dano terminal](terminal_damage.md) — insta crash (F11), pausa e Reiniciar durante a destruição, o gate `host+0x2350` e o que fica preso depois
- [Carregamento de especiais](stage_loading.md) — resolução de pistas, `BenchmarkManager`, hook do `AutoStage`, `LoadTrace`
- [Travamentos da carga](loading_hangs.md) — usar uma carga travada como sonda do carregamento
- [Carregamento de pista](track_loading.md) — `raceload.jpk`/`track_loader.xml`, tokens, processadores, PSSG→GPU e a LoadProbe
- [Formatos de pista](track_formats.md) — o que já foi decifrado em `locations/*.nefs` (terreno, objetos, árvores, ornamentos, traçado, JPAK) e o que falta; usado pelo Track Explorer ([../UIVIEW.md](../tools/uiview.md))
- [Fantasmas](ghosts.md) — saves `GHST`, slots, cópias, `GhostCarValues`, a curva de 5–50 m, o descarte com fator 1,0 (fantasma sólido), colisão, pausa dos fantasmas e o máximo de 15 fantasmas + jogador

`python3 -m tools.dr2rec` grava a sessão e analisa depois. O manual está em [../BLACKBOX.md](../tools/dr2rec.md). O recorder não reinterpreta os bytes. As âncoras que eram as seções 10.14–10.18 entram na captura para comparação, e o analyzer não reabre o significado delas. O byte em `+0x2cf0` fica fora da janela `0x0000:0x2600`; para observá-lo, a região precisa cobrir esse offset. A ferramenta só lê. Não escreve no processo e não fala com a RaceNet.

## Como a leitura vive no mod

A cadeia e os offsets são lidos por `src/core/player.cpp`, ligado em `dr2hook_core.dll`, não na proxy. A marcha na UI é o `int32` em `+0x1448`. O conta-giros é o `float` em rad/s em `+0x13d8`. O corte desenhado na barra é `+0x140c`. `+0x918` não entra no conta-giros. O significado de cada campo está em [Gearbox](gearbox.md) e [Engine](engine.md).

Para experimentar um offset novo: recompile só `dr2hook_core.dll`, substitua esse arquivo na pasta do jogo e pressione **F8**. A `dxgi.dll` e os hooks de rede permanecem os da sessão. O isolamento está em [Network Guard](network_guard.md).

## Comandos internos de depuração

Strings associadas ao console interno do executável:

1. `"debug.global.teleport.pressed"`: comando de trigger interno utilizado pelos desenvolvedores para reposicionar veículos durante testes de colisão e malha.
2. `"reset_vehicle"`: rotina padrão de recuperação que reposiciona o veículo na pista aplicando penalidade de tempo de jogo.
3. `"vehicle_manager"`: nome canônico registrado na tabela central de subsistemas em `.data`.

Não há, neste material, mapa de colisão, malha ou implementação dessas rotinas.

## Duas bases de offset

O texto original usa as duas. A correspondência está em [PhysicsRig](physics_rig.md).

- Cabeçalho do vetor da roda em `rig + 0x1680 + i·0x420`. Nesse texto, `+0x00`, `+0x10`, `+0x20` e `+0x30` são relativos a esse cabeçalho.
- Objeto de `0x420` bytes construído a partir de `rig + 0x1480`. Os campos `+0x1480`, `+0x1504`, `+0x1660` e os vizinhos são `rig + i·0x420 + deslocamento`.

## Assuntos sem documento próprio

Não havia material suficiente para um arquivo só deles:

| Assunto | O que o arquivo contém | Onde ficou |
| :--- | :--- | :--- |
| Áudio, replay | `DynamicsCar` é interface exposta a esses subsistemas. Sem offset próprio. | [PhysicsRig](physics_rig.md) |
| Freios | Só `brake_temperature` nos offsets UDP `204`–`216`. Sem campo interno. | [Telemetry](telemetry.md) |
| Colisão | Só o propósito da string de teleporte, acima. | Este índice |
| Rotação de roda | Nomes `wheel_rotation_rate` / `wheel_speed`; offset interno não isolado. | [Wheels](wheels.md) |
| Temperatura, pressão, grip, desgaste, composto | Strings ou ausência de campo. | [Tyres](tyres.md) |

## Mapa do arquivo monolítico

| Seção antiga | Documento |
| :--- | :--- |
| 1. Binário e seções PE | [Executable](executable.md) |
| 2. Ponteiros globais | [PhysicsRig](physics_rig.md) |
| 3. Cadeia do veículo | [PhysicsRig](physics_rig.md) |
| 4. Layout do Physics Rig | [PhysicsRig](physics_rig.md), com a semântica nos documentos de cada campo |
| 5. RTTI | [PhysicsRig](physics_rig.md) |
| 6. Telemetria UDP | [Telemetry](telemetry.md) |
| 7. Comandos de depuração | Este índice |
| 8. Winsock | [Network Guard](network_guard.md) |
| 9. Onde a leitura vive | Este índice; campos em [Engine](engine.md) e [Gearbox](gearbox.md) |
| 10.1–10.6, 10.8–10.10 | [Wheels](wheels.md); linhas de dano em [Damage](damage.md); byte do pneu em [Tyres](tyres.md) |
| 10.2 | [Vehicle Transform](vehicle_transform.md) |
| 10.7, 10.12–10.15 | [Suspension](suspension.md) |
| 10.11 | [Telemetry](telemetry.md) |
| 10.16–10.17 | [Vehicle Setup](vehicle_setup.md) |
| 10.18 | [Tyres](tyres.md); canais de dano em [Damage](damage.md) |
| 11. Caixa-preta | [../BLACKBOX.md](../tools/dr2rec.md) e o parágrafo acima |
