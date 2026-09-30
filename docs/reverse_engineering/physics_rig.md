# PhysicsRig

Referência estrutural do `DynamicsCarImpl`. O significado de cada campo está no documento do subsistema. Este arquivo guarda a cadeia, o tamanho, o passo e onde cada bloco começa.

## Overview

A EGO Engine organiza módulos por descritores de subsistemas na seção `.data` e por instâncias no heap durante o carregamento de fases. O Physics Rig é o bloco de heap onde estão as variáveis integradas pelo solver de física rígida a cada tick.

`DynamicsCar` é a interface abstrata exposta a subsistemas externos (câmera, áudio, replay). `DynamicsCarImpl` é a classe concreta de cálculo numérico. Tamanho citado no RTTI: `0x3130`. Não há, neste material, offsets de câmera, áudio ou replay.

## Known Structures

```text
.??_R0?AVDynamicsCar@@@8
  ├── .??_R1A@?0A@EA@DynamicsCar@@8
  └── .??_R2DynamicsCar@@8
        └── .??_R3DynamicsCar@@8
              └── .??_R4DynamicsCar@@6B@ (Complete Object Locator)

.??_R0?AVvehicleDynamics@@@8
  └── namespace neon::vehiclePhysics
        └── Codemasters::Ego::DynamicsCarImpl (Tamanho: 0x3130)
```

Os descritores foram buscados em `.rdata`. As seções do executável estão em [Executable](executable.md).

### Cadeia até o rig

Inspeção em tempo real, sem hook no loop de física. Endereços de heap abaixo são exemplos, não bases fixas.

```text
[dirtrally2.exe + 0x1681ce8]  (ou fallback +0x15a4b00 / +0x15a9760)
           │
           ▼
        Car*       (Heap: ex. 0x4a9d4b80 — "car 1")
           │
           │  +0x30 (container de física do veículo)
           ▼
     Container*    (Heap: ex. 0x4aa0f100 — vtable dirtrally2.exe + 0x1400c30)
           │
           │  +0x08 (Physics Rig / DynamicsCarImpl concreto)
           ▼
    Physics Rig*   (Heap: ex. 0x4aa1bab0)
```

Validação usada pelo mod:

1. **`gameBase`**: `GetModuleHandleA(nullptr)` (RVA base `0x140000000`).
2. **`car`**: leitura em `gameBase + 0x1681ce8`. Se nulo, fallbacks `+0x15a4b00` ou `+0x15a9760`.
3. **`container`**: 8 bytes em `car + 0x30`. Se nulo, `gameBase + 0x201b7c0` ou `gameBase + 0x20203a8`.
4. **`physicsRig`**: 8 bytes em `container + 0x08`.

Ponteiros globais em `.data`:

| Símbolo no material | RVA |
| :--- | :--- |
| Ponteiro primário do veículo ativo (sessão do jogador) | `dirtrally2.exe + 0x1681ce8` |
| Ponteiro da tabela de veículos (carro 1 / player rig) | `dirtrally2.exe + 0x15a4b00` |
| Ponteiro de corrida ativa (race instance) | `dirtrally2.exe + 0x15a9760` |
| Fallbacks estáticos do container | `dirtrally2.exe + 0x201b7c0` e `+0x20203a8` |

Rigs citados nas medições, cada um um endereço de heap daquela sessão: `0x4b1abab0`, `0x4b76bab0`, `0x4b29bab0`, e o exemplo `0x4aa1bab0`.

### Duas bases do bloco de roda

A ordem atual dos quatro blocos é RL, RR, FL, FR. O histórico do rótulo antigo está em [Wheels](wheels.md). O passo é `0x420`.

| i | Canto | Cabeçalho do vetor, literal do material | Objeto de `0x420` |
| :--- | :--- | :--- | :--- |
| 0 | RL | `rig + 0x1680` | `rig + 0x1480` |
| 1 | RR | `rig + 0x1aa0` | `rig + 0x1480 + 1·0x420` |
| 2 | FL | `rig + 0x1ec0` | `rig + 0x1480 + 2·0x420` |
| 3 | FR | `rig + 0x22e0` | `rig + 0x1480 + 3·0x420` |

Os quatro cabeçalhos são literais do material. A base do objeto é a do construtor, `rig + 0x1480`, mais `i·0x420`. O construtor `0x14072fd20` é chamado com esse passo a partir daí (`0x140746bfc`, `0x1407471f4`). O offset `0x210` desse objeto é onde cai o `+0x10` do cabeçalho: para `i = 0`, `0x1480 + 0x210 = 0x1690`. A distância do início do objeto até o cabeçalho, no primeiro bloco, é `0x1680 − 0x1480 = 0x200`.

Há ainda, na medição de `+0x270`, um bloco `+0x1260` cujo `+0x270` cai em `rig + 0x14d0` e que não tem o mesmo `+0x10`. Essa observação está em [Wheels](wheels.md) e não foi promovida a quinto canto.

### Subobjeto da tabela 4×4

O construtor `0x140746b8a` faz `lea rcx, [rbx + 0x11a0]` e grava `rbx` em `[rcx + 0x120]`. Com `rbx` igual ao rig, `[rig + 0x12c0]` aponta para o próprio rig. Ao vivo o ponteiro é o rig e `[rig + 0x12d0]` vale `4`. Os 16 floats ficam em `rig + 0x12dc` (`+0x13c` do subobjeto). O nome do subobjeto fica em aberto. A conta está em [Wheels](wheels.md).

## Memory Layout

Campos cujo significado está noutro documento. A confiança da semântica é a de lá.

| Offset | Tipo | Papel estrutural | Semântica |
| :--- | :--- | :--- | :--- |
| `+0x180`, `+0x210` | vetor | Blocos no rig, fora da roda | [Vehicle Physics](vehicle_physics.md) |
| `+0x2b0`, `+0x2c0` | vetores | `T` e `V`; copiados para as velocidades | [Vehicle Physics](vehicle_physics.md) |
| `+0x2d0` | `Vector3` 16 B | Posição do centro de massa | [Vehicle Transform](vehicle_transform.md) |
| `+0x2e0` | `Vector4` 16 B | Quatérnion | [Vehicle Transform](vehicle_transform.md) |
| `+0x2f0`, `+0x300`, `+0x310` | `Vector3` 16 B | Right, Up, Forward | [Vehicle Transform](vehicle_transform.md) |
| `+0x320`, `+0x330` | `Vector3` 16 B | Velocidade linear e angular | [Vehicle Physics](vehicle_physics.md) |
| `+0x370` | `float` | Escalar lido pela conta das rodas | [Vehicle Physics](vehicle_physics.md) |
| `+0x4c0` | qword | Comparada com `[[rig + 0x1120] + 0xe8]` para recarregar o setup | [Vehicle Setup](vehicle_setup.md) |
| `+0x4d0` | bloco | Setup decodificado. Pontos em `+0x5a0` e `+0x5b0` | [Vehicle Setup](vehicle_setup.md) |
| `+0x8e8`, `+0x918`, `+0x13d8`, `+0x140c` | `float` | Motor | [Engine](engine.md) |
| `+0x8f4`, `+0x1400`, `+0x1448` | `float` / `int32` | Câmbio | [Gearbox](gearbox.md) |
| `+0x1120` | ponteiro | Quarto argumento da inicialização do rig | [Vehicle Setup](vehicle_setup.md) |
| `+0x1130`, `+0x1190` | seis vetores e um byte | Bloco alternativo usado em `+0x1490` | [Vehicle Setup](vehicle_setup.md) |
| `+0x11a0` | subobjeto | Tabela 4×4 em `+0x12dc`; self em `+0x12c0`; contagem em `+0x12d0` | [Wheels](wheels.md) |
| `+0x1480 + i·0x420` | objeto `0x420` | Ponto `L`, curso, eixos da roda | [Wheels](wheels.md), [Suspension](suspension.md), [Vehicle Setup](vehicle_setup.md) |
| `+0x2bd0` | objeto | Byte de estado em `+0x220 + i`; acumulado em `+0x130 + 4·i` | [Tyres](tyres.md) |
| `+0x2cf0` | `byte[4]` | Estado do pneu e da roda, fora do bloco de `0x420`. Ordem RL, RR, FL, FR | [Tyres](tyres.md) |
| `+0x2d00` | `float[4]` | Acumulado por roda, `rig + 0x2bd0 + 0x130 + 4·i` | [Tyres](tyres.md) |
| `container + 0xcd0` | `x, y, z, yaw` | Âncora visual, não está no rig | [Vehicle Transform](vehicle_transform.md) |

Dentro do objeto que começa em `rig + 0x1480 + i·0x420`, os deslocamentos usados no material, para `i = 0`, caem nos endereços absolutos já citados (`+0x1480`, `+0x14e8`, `+0x1504`, `+0x1560`, `+0x1660`, `+0x1670`, `+0x1680`, `+0x1690`). O mapa campo a campo está nos documentos de semântica, não repetido aqui.

## Functions

| Endereço | Função | Relevância |
| :--- | :--- | :--- |
| `0x140746b8a` | Construtor que ancora o subobjeto em `rig + 0x11a0` | Grava o self-pointer em `+0x12c0`. |
| `0x1407469fa` / `0x140746b5f` | Inicialização do rig | `[rig + 0x1120]` recebe o quarto argumento só em `0x140746b5f`. |
| `0x14072fd20` | Construtor do objeto de `0x420` | Chamado desde `rig + 0x1480`. Zera o começo do bloco. Não escreve o offset `0x210` (`+0x10` da roda). |

## Confirmed

- A cadeia `+0x1681ce8` → `car + 0x30` → `container + 0x08`, com os fallbacks listados.
- `DynamicsCarImpl` tem tamanho `0x3130` no RTTI, dentro de `neon::vehiclePhysics`.
- Quatro objetos de passo `0x420` a partir de `rig + 0x1480`, e quatro cabeçalhos a partir de `rig + 0x1680`, na ordem RL, RR, FL, FR.
- O subobjeto em `+0x11a0` contém o ponteiro para o próprio rig e a contagem `4`.

## Unknown

- Nome do subobjeto em `+0x11a0`.
- Papel do bloco `+0x1260`.
- Campos de câmera, áudio e replay na interface `DynamicsCar`.

## Related Systems

- [Wheels](wheels.md)
- [Suspension](suspension.md)
- [Tyres](tyres.md)
- [Vehicle Setup](vehicle_setup.md)
- [Vehicle Transform](vehicle_transform.md)
- [Vehicle Physics](vehicle_physics.md)
- [Engine](engine.md)
- [Gearbox](gearbox.md)
