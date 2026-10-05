# Câmera

## Overview

O `dirtrally2.exe` desta árvore (ImageBase `0x140000000`, o mesmo binário de [Executable](executable.md)) tem dois caminhos no mesmo objeto de câmera. O caminho da especial escreve o bloco em `+0x200`, e o olho do mundo fica em `+0x240`. O caminho de depuração, que lê mouse e eixos de teclado, não corre nesse estado: `+0x3c6` é 0 e `+0x360` não é nulo, então `+0x3d4` não é consultado.

A leitura ao vivo abaixo só lê `/proc/<pid>/mem`. Nada foi escrito no processo.

## Known Structures

Cada câmera é um objeto no heap. O tick percorre um array de ponteiros e chama `0x140a51ea0` em cada ponteiro não nulo.

O dono do tick é o objeto publicado em `0x14168caf0`. O construtor `0x140466f20` grava a vtable `0x14127a030` em `[rcx]` e o próprio `rcx` nesse global. O slot `+8` dessa vtable é `0x1404b26f0`, que chama o loop `0x1404ac0c0`. No mesmo construtor, `r8` vai para `[rcx+0x20]` e para `0x1416b0ea0`.

| O quê | Onde | Confiança |
| :--- | :--- | :--- |
| Dono | `[dirtrally2.exe + 0x168caf0]`, vtable `0x14127a030` | CONFIRMADO ao vivo: vtable bate, pid `2050923` |
| Mundo | `[dono + 0x20]`, também `[exe + 0x16b0ea0]` | CONFIRMADO ao vivo: os dois ponteiros eram iguais |
| Quantidade | `([objeto + 0x128] − [objeto + 0x120]) / 8`, com `objeto = [mundo + 0x1ae8]` | Análise estática. Ao vivo o vetor tinha 1 elemento |
| Câmera `i` | `[mundo + 0x1cf8 + i·8]` | CONFIRMADO ao vivo para `i = 0`. Os slots seguintes não fazem parte do vetor |
| Tick de uma câmera | `0x140a51ea0` | Análise estática. Chamada em `0x1404ac1df` |

`0x1404b26f0` não tem `call` direto. O qword está em `0x14127a038`.

## Memory Layout

Offsets do objeto passado em `rcx` para `0x140a51ea0` e para `0x140a5c590`. Os dois usam o mesmo `this`.

| Offset | Tipo | O que o código faz | Confiança |
| :--- | :--- | :--- | :--- |
| `+0xe0` | bloco | Destino passado a `0x140a285a0` dentro de `0x140a5c590`, no caminho de depuração | Análise estática. Ao vivo era um bloco default: eixos de base e translação `(0, 10, 40)`, não o olho da especial |
| `+0x130` | `float` | Base da sensibilidade de rotação. Multiplicada por `0.02` antes do expoente de velocidade | Análise estática |
| `+0x200` | bloco | No caminho da especial, `rdx` de `0x140a285a0` / `0x140a28630`. O olho do mundo está em `+0x240` | CONFIRMADO ao vivo, tabela abaixo |
| `+0x210` | `Vector3` (16 B, `w = 0`) | Cima, no mundo | CONFIRMADO ao vivo. Quase alinhado com o cima do rig |
| `+0x220` | `Vector3` (16 B, `w = 0`) | Direita. `direita × cima = frente` | CONFIRMADO ao vivo. Coincide com `rig + 0x2f0` |
| `+0x230` | `Vector3` (16 B, `w = 0`) | Frente | CONFIRMADO ao vivo. Coincide com `rig + 0x310`, um pouco para baixo |
| `+0x2d0` | 4× `xmm` (64 B) | Matriz. A translação com `w = 1` está em `+0x300`, perto do carro e abaixo do olho | CONFIRMADO ao vivo |
| `+0x384` | `int32` | Expoente de velocidade. `debug.camera.speed.slower` decrementa, piso `-3`. `debug.camera.speed.faster` incrementa, teto `+3` | Análise estática |
| `+0x388` | `int32` | Precisa ser `0` para o toggle e para o movimento de depuração | Análise estática. Significado do valor: UNKNOWN |
| `+0x3b0` | `float` | Multiplicado por `60` e aplicado à translação e à rotação do frame | Análise estática. Candidato a `dt`; não medido |
| `+0x3c6` | `uint8` | Com `0` e `+0x360` não nulo, o tick submete `+0x200` e não chama o input de depuração | CONFIRMADO ao vivo: valia `0` |
| `+0x3c8` | `uint8` | Se não for `0`, o input de depuração não roda | Análise estática |
| `+0x3d4` | `uint8` | Liga a câmera livre. O toggle faz `byte ^= 1` quando as duas ações abaixo disparam juntas | Análise estática |
| `+0x470` | `uint8` | Se não for `0`, o bloco de eixos é saltado | Análise estática |
| `+0x478` | ponteiro | Contexto de input. Primeiro argumento de `0x140d97470`, `0x140d97410` e `0x140d96e10` | Análise estática |
| `+0x480` | `int32` | Último X do cursor usado pelo mouse | Análise estática |
| `+0x484` | `int32` | Último Y do cursor usado pelo mouse | Análise estática |

O desvio em `0x140a52016` é exclusivo:

- `+0x3c6 == 0` e `+0x360 != 0`: não chama `0x140a5c590`. Submete `câmera + 0x200`.
- Caso contrário, se `+0x3c8 == 0`: chama `0x140a5c590`. Esse caminho, se `+0x360` não é nulo, passa `câmera + 0xe0` para `0x140a285a0`.

`0x140a285a0` não lê a câmera livre. Chama `[view + 0xc0]` e copia o retorno para o bloco de destino, a partir de `+0x10` desse bloco.

## Câmera livre

`0x140a51ea0` chama `0x140a5c590` (uma função, dois registros em `.pdata`: `0x140a5c590`–`0x140a5d8b6` e `0x140a5d8b6`–`0x140a5f8bd`). A segunda metade continua a primeira: o `jne` em `0x140a5d8ce` usa o `cmp` de `[this + 0x470]` feito em `0x140a5d8a6`.

### Liga e desliga

Com `[this + 0x478]` válido e `[this + 0x388] == 0`:

1. `0x140d97410` em `debug.camera.modifier.input` precisa devolver verdadeiro (ação segura).
2. `0x140d97470` em `debug.camera.toggle.enable.p1` precisa devolver verdadeiro (borda).
3. Aí `[this + 0x3d4]` inverte.

O movimento só corre se `[this + 0x3d4] != 0`, `[this + 0x388] == 0` e `debug.camera.modifier.save` não estiver segura. `modifier.input` segura troca o fator `1.0` por `3.0`.

As teclas físicas dessas ações não estão nesta função. Os nomes são internados em slots fixos de `.data` por `0x1408349d0` (lazy init da CRT). O slot é um ponteiro seguido de um guard de 4 bytes, stride `0x10`, a partir de `0x141f61900`.

| Slot | Ação |
| :--- | :--- |
| `0x141f61900` | `debug.camera.toggle.enable.p1` |
| `0x141f61910` | `debug.camera.modifier.input` |
| `0x141f61920` | `debug.camera.speed.slower` |
| `0x141f61930` | `debug.camera.speed.faster` |
| `0x141f61940` | `debug.camera.speed.temp_faster` |
| `0x141f61950` | `debug.camera.modifier.save` |
| `0x141f61970` | `debug.camera.keyboard.move` |
| `0x141f61980` | `debug.camera.keyboard.move.up` |
| `0x141f61990` | `debug.camera.keyboard.move.down` |
| `0x141f619a0` | `debug.camera.keyboard.move.left` |
| `0x141f619b0` | `debug.camera.keyboard.move.right` |
| `0x141f619c0` | `debug.camera.keyboard.move.forward` |
| `0x141f619d0` | `debug.camera.keyboard.move.backward` |
| `0x141f619e0` | `debug.camera.keyboard.turn.pitch.up` |
| `0x141f619f0` | `debug.camera.keyboard.turn.pitch.down` |
| `0x141f61a00` | `debug.camera.keyboard.turn.yaw.up` |
| `0x141f61a10` | `debug.camera.keyboard.turn.yaw.down` |
| `0x141f61a20` | `debug.camera.keyboard.turn.roll.up` |
| `0x141f61a30` | `debug.camera.keyboard.turn.roll.down` |
| `0x141f61ca0` | `debug.camera.mouse.pitch` |
| `0x141f61cb0` | `debug.camera.mouse.wheel.up` |
| `0x141f61cc0` | `debug.camera.mouse.wheel.down` |

Há o mesmo conjunto para o gamepad (`debug.camera.pad.*`), quatro slots `save_load`, `reset`, `toggle.link.p1` / `p2`, eixos `look_at.*`, e nomes `debug.camera.unknown.*` (`r`, `t`, `f`, `o`, `i`, `y`, `u`, `h`, `g`). O texto de cada um está em `.rdata` entre `0x1413a5820` e `0x1413a5f90`.

Consultas vistas:

| Função | Retorno | Uso aqui |
| :--- | :--- | :--- |
| `0x140d97470` | `al` | borda (velocidade, toggle) |
| `0x140d97410` | `al` | nível (modificadores) |
| `0x140d96e10` | `xmm0` | analógico dos eixos |

### Escala do frame

`n` começa em `[this + 0x384]`. Se `speed.temp_faster` estiver segura, `n` soma 1 na cópia local, sem novo clamp. O campo gravado fica em `[-3, +3]`; a cópia pode chegar a `4`.

```text
s = [this + 0x3b0] * 60
trans = 0.3 * (5 ^ n) * s          n >= 0
trans = 0.3 * (0.05 ^ |n|) * s     n < 0
turn  = ([this + 0x130] * 0.02) * (1.5 ^ n) * s       n >= 0
turn  = ([this + 0x130] * 0.02) * (0.2 ^ |n|) * s     n < 0
```

O terceiro fator (`0.025` e `1.1` / `0.1`) segue o mesmo expoente. O sítio que consome esse terceiro fator não foi isolado.

Cada eixo de translação é `poll(positivo) − poll(negativo)`, vezes `trans`, vezes `1` ou `3`. O resultado vai para locais de pilha (`rbp + 0x1b0` para cima/baixo, `rbp + 0x1c8` para esquerda/direita). A gravação desses locais na matriz de visão fica no resto da mesma função e ainda não foi seguida instrução a instrução.

### Mouse

Não há ação `mouse.yaw`. Os dois eixos saem da posição do cursor quando `poll(debug.camera.mouse.pitch)` não é zero:

1. `0x140a043f0` devolve um inteiro em `eax`.
2. `0x140d96310(contexto)` devolve o outro inteiro em `eax`.
3. `dx = (x_anterior − x_novo)`, `dy = (y_anterior − y_novo)`, com os anteriores em `+0x480` e `+0x484`.
4. `dx` vira float, multiplica por `turn` e por `-0.3` (`0x14127514c`).
5. `dy` vira float, multiplica por `turn` e por `+0.3` (`0x141227fcc`).
6. Os inteiros novos substituem `+0x480` e `+0x484`.

Se o analógico do mouse é zero, o mesmo trecho usa pitch, yaw e roll do teclado, também escalados por `turn`. O roll do teclado cai em `rbp + 0x1b8`.

## Pose do veículo copiada para a câmera

Ainda em `0x140a51ea0`, antes do input de depuração, se `[0x14159daf0]` não é nulo:

```text
idx  = [câmera + 0x38c]          ; int32 com sinal
node = [[0x14159daf0] + 0xf8] + idx*8
con  = [node + 0x30]
```

`0x140731610(con)` decodifica sete dwords em `[con + 0x8] + 0x258` com multiplicações inteiras e os reinterpreta como `float`. `0x140660060` monta quatro linhas SIMD a partir desses floats e a função grava o resultado em `câmera + 0x2d0`, `+0x2e0`, `+0x2f0` e `+0x300`.

`[con + 0x8]` é o mesmo salto do container para o rig em [PhysicsRig](physics_rig.md). O bloco decodificado fica em `rig + 0x258`. A pose confirmada do chassi está em `rig + 0x2d0`. As duas regiões são vizinhas; a igualdade dos valores não foi medida.

`0x14159daf0` não é o ponteiro de veículo `exe + 0x1681ce8`.

## Câmera de jogabilidade

Nomes no executável, sem offset de instância:

- Modos: `enable_camera_bonnet`, `enable_camera_bumper`, `enable_camera_chase_close`, `enable_camera_dashcam`, `enable_camera_gopro`, `enable_camera_headcam`, e os de replay `enable_camera_replay_heli`, `enable_camera_replay_drone`, `enable_camera_replay_action`, `enable_camera_replay_gopro`.
- Sistema: string `CameraSystem` referida em `0x140a19fb4`, dentro de `0x140a19d80`. A mesma função cita `dynamicHelicopterViewManager` e `trackChaseViewManager` e chama `0x140a1b0f0` para os dois.
- Por carro: `/data/cars/models/%s/cameras.xml`, usada em `0x14050f2ea`.
- `cycle_camera` do benchmark é outro byte, no bloco de configuração da especial. Ver [stage loading](stage_loading.md).

## Leitura ao vivo

Pid `2050923`, `dirtrally2.exe` em `0x140000000`, carro parado. Uma amostra.

| Campo | Valor |
| :--- | :--- |
| Dono `0x14168caf0` | `0x37400380`, vtable `0x14127a030` |
| Mundo | `0x1582a340`, igual a `0x1416b0ea0` |
| Vetor | 1 câmera, `0x158ffdc0`, vtable `0x1413a0f90` |
| `+0x3c6`, `+0x3c8`, `+0x3d4`, `+0x470` | `0`, `0`, `0`, `0` |
| `+0x360` | `0x173897ab0` |
| `+0x478` | `0` |
| Rig `+0x2d0` | `(-4578.00, 124.93, 39.78)` m |
| Olho em `câmera + 0x240` | `(-4578.24, 125.64, 39.19, 0)` m |
| Cima `+0x210` | `(0.041, 0.999, 0.015, 0)` |
| Direita `+0x220` | `(0.597, -0.012, -0.802, 0)` |
| Frente `+0x230` | `(0.801, -0.042, 0.597, 0)` |
| Translação em `câmera + 0x300` | `(-4578.29, 124.58, 39.57, 1)` |

O olho está cerca de `0,7 m` acima do centro de massa e um pouco ao lado. `+0xe0` nessa hora tinha translação `(0, 10, 40)`, o bloco default, não a especial. `+0x478` nulo: o toggle nativo nem chega a consultar a ação, porque o contexto de input falta. `debug.camera` não aparece em `input/`, nem em `game.dat`, `game_1.dat` ou `game.nefs`. O que o teclado liga, em `input/actionmaps/keyboard.xml`, é `driving.change_view` na tecla `C` (`vk_code_0x43`) e `driving.look.*` em outras teclas. Não há WASD de câmera livre nesse mapa.

## Mod de câmera livre

`src/core/free_camera.cpp`, no `dr2hook_core.dll`. **F9** liga e desliga. O hook é o tick `0x140a51ea0`: a função nativa corre primeiro e preenche `+0x200`; em seguida o mod substitui `+0x210`, `+0x220`, `+0x230` e `+0x240` da câmera publicada em `[mundo + 0x1cf8]`.

Na ligação, a frente capturada fica. A direita e o cima são refeitos sem roll, com `+Y` do mundo para cima. O mouse gira (direita olha à direita, para baixo olha para baixo). **W/S** andam na frente, **A/D** na direita da imagem, **Espaço/Q** no cima. **Ctrl** segurado zera a translação do teclado e deixa o mouse. **+** e **−** (tecla da fileira e do teclado numérico) sobem e descem a velocidade do teclado entre `1,25` e `320` m/s, começando em `20`. **Shift** multiplica essa velocidade por 4. Com o `StatePauseScreen` na pilha do fluxo (`[exe+0x16951e0]+0x28`, pilha em `runner+0x38`), o cursor volta e a pose fica parada; ao sair da pausa a captura retoma. O yaw positivo da base e o eixo `direita` apontam para a esquerda da imagem, então o mouse e o **A/D** entram com o sinal trocado. Com o menu (Insert) aberto, a pose fica parada e o cursor volta.

O `w` do olho é preservado. Nesta sessão era `0`. `+0x250` (nesta sessão `1.086`, `0.05`, `1000`) não é escrito.

## Confirmed

- O dono está em `exe + 0x168caf0` e o mundo em `[dono + 0x20]`, também em `exe + 0x16b0ea0` nesta sessão.
- Na especial, com o carro parado, a câmera 0 está no caminho `+0x200`. `+0x3d4` é 0 e não é lido.
- O olho do mundo nessa hora é o `Vector3` em `câmera + 0x240`, com `w = 0`.
- `+0x210`, `+0x220` e `+0x230` são cima, direita e frente. `direita × cima = frente`.
- `debug.camera.*` não está nos XML de input soltos nem nesses três arquivos de `game/`.

## Unknown

- Quem, depois de `0x140a285a0`, lê `câmera + 0x240` para a GPU.
- O nome do escalar em `câmera + 0x200` (nesta sessão, `10.234`).
- Se `rig + 0x258` decodificado coincide com a matriz em `câmera + 0x2d0`.
- Layout de bonnet, bumper e headcam. Esta amostra é uma câmera só, com o carro parado.

## Related Systems

- [PhysicsRig](physics_rig.md) — cadeia até o rig; a pose confirmada começa em `rig + 0x2d0`
- [Vehicle Transform](vehicle_transform.md) — posição, quatérnion e base do chassi
- [stage loading](stage_loading.md) — `player_camera` no staging e o byte `cycle_camera`
