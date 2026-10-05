# Suspension

## Overview

O curso por roda é um escalar no objeto de `0x420` bytes. Para a roda `i` o endereço é `[rig + i·0x420 + 0x1504]`. A ordem atual dos blocos é RL, RR, FL, FR. Os vetores do cabeçalho estão em [Wheels](wheels.md). O ponto do eixo que este curso desloca vem de [Vehicle Setup](vehicle_setup.md).

Conclusão atual: `float32(escalar × 1000)` é, bit a bit, o `suspension_position` UDP da mesma roda. Antes dessa captura, a ligação não existia no código.

## Memory Layout

| Offset | Tipo | Descrição | Confiança |
| :--- | :--- | :--- | :--- |
| `rig + 0x1504` | `float` | Curso da RL (`i = 0`, bloco `+0x1680`). | CONFIRMADO contra o UDP |
| `rig + 0x1924` | `float` | Curso da RR (bloco `+0x1aa0`). | CONFIRMADO contra o UDP |
| `rig + 0x1d44` | `float` | Curso da FL (bloco `+0x1ec0`). | CONFIRMADO contra o UDP |
| `rig + 0x2164` | `float` | Curso da FR (bloco `+0x22e0`). | CONFIRMADO contra o UDP |
| `+0x1500` no mesmo passo | `float` | Termo integrado. No caminho comum entra clamped em `[-20, 20]`. Zerado num dos outros ramos. | Papel no integrador observado no código; nome físico não dado |
| `roda + 0x16cc` | `float` | Comparado com `+0x1504` em `0x14073ea80`. | Uso observado; nome não dado |
| `+0x1660` | vetor | `base + [+0x1504] * Up`. Não é o escalar. Não é uma posição de mundo. | Construção CONFIRMADA no rig medido; nome no executável ausente |
| `+0x1670` | vetor | A mesma `base`, sem o termo de suspensão. | CONFIRMADO como store do escritor |
| `+0x16d0` | escalar | `dot(Up, T + V × P)`, com `P` = `+0x1660`. | Store observado; nome não dado |

`Up` no escritor de `+0x1660` é o Up do chassi, gravado em `+0x1680` na mesma volta. `T` e `V` estão em [Vehicle Physics](vehicle_physics.md).

## Functions

| Endereço | Função | Relevância |
| :--- | :--- | :--- |
| `0x140736cc0` | Função que contém a gravação principal | Começo da função do curso. |
| `0x1407376ed` | Store principal de `+0x1504` | Caminho comum abaixo. |
| `0x14073f220` | Recebe `h` em `xmm1` | Divide `h` pelo número de subpassos `N`. O integrador usa `N/h`. |
| `0x14073ea80` | Compara `+0x1504` com `[roda + 0x16cc]` | O laço separa `i < 2` de `i >= 2` ao ler limites diferentes. |
| `0x140748892` | Soma `[+0x1504] * (+0x00)` ao ponto que termina em `+0x1660` | Antes disso o campo recebe o retorno de `0x14072f8e0`. Descrição anterior ao fechamento de `0x140748670`. |
| `0x140748670` | Escritor por roda de `+0x1660` e `+0x1670` | Fórmula fechada abaixo. Também grava o Up do chassi em `+0x1680`. |
| `0x14072f8e0` | `q * v * conjugado(q)` | Não lê o rig. Não é rotina de roda. Ver [Vehicle Transform](vehicle_transform.md). |
| `0x14073e150` | Lê `+0x1660` logo depois do escritor | Grava `+0x16d0`. |
| `0x140749292` | Soma a posição do chassi a `+0x1660` | Passa o resultado a uma chamada virtual. O destino dessa chamada não foi identificado. |
| `0x14073a1b0` | Soma a posição do chassi a `+0x1670` | O vetor sem o termo de suspensão. |

## Data Flow

```text
conta do passo
    │
    ▼
novo_+0x1500 = clamp(conta, -20, 20)          caminho comum
    │
    ▼
novo_+0x1504 = antigo_+0x1504 + (N / h) * novo_+0x1500
    │
    ▼
L em +0x1480, c em +0x14e8          ver Vehicle Setup
    │
    ▼
v = (L.x, L.y - c, L.z)
q = quatérnion em rig + 0x2e0
base = q * v * conjugado(q)
    │
    ├── [+0x1670] = base
    └── [+0x1660] = base + [+0x1504] * Up
                │
                ├── 0x14073e150 → +0x16d0 = dot(Up, T + V × P)
                ├── 0x140749292 → posição do chassi + +0x1660 → virtual, destino desconhecido
                └── 0x14073a1b0 → posição do chassi + +0x1670
```

`h` é o escalar que entra no passo do veículo em `xmm1` e desce até `0x14073f220`. Ali ele é dividido pelo número de subpassos `N`. Não está provado que `h` seja o passo de tempo.

Outros ramos da mesma função substituem `+0x1504` por um float da pilha (`[rdx + 0xc]`) ou por um limite, e zeram `+0x1500` num desses ramos.

O agrupamento `i < 2` / `i >= 2` junta os blocos em dois pares. Não diz qual par é o eixo dianteiro. Quem identificou os cantos foi a captura, não esse desvio.

Na equação das rodas, `P1` soma a posição do chassi, o vetor devolvido por `0x14072f8e0` e `[roda + 0x1504] * (+0x00)`. `B` é derivado de `+0x1660` por uma conta da mesma família. Ver [Wheels](wheels.md).

## Mathematics

No escritor `0x140748670`, por roda:

```text
L = três floats em rig + i*0x420 + 0x1480
c = [rig + i*0x420 + 0x14e8]
v = (L.x, L.y - c, L.z)
q = quatérnion em rig + 0x2e0
base = q * v * conjugado(q)
[+0x1670] = base
[+0x1660] = base + [+0x1504] * Up
```

No rig `0x4b76bab0`, o erro dessa igualdade ficou em `4e-5`. `|P|` ficou entre `1,27` e `1,76`, com o chassi em `(-153,6, 318,0, -413,5)`. `P` não é uma posição de mundo.

`L` nesse instante forma o retângulo já medido: `x = ±0,712`, `z = 0,935` nas dianteiras e `-1,538` nas traseiras. `c` valeu `0,301` nas quatro. A origem de `L` e de `c` está em [Vehicle Setup](vehicle_setup.md).

`0x14073e150`, com `P` esse vetor, `T` em `rig + 0x2b0` e `V` em `rig + 0x2c0`, grava em `+0x16d0` o produto `dot(Up, T + V × P)`. `T` e `V` são os campos copiados para a velocidade linear e a angular.

Identidade com o pacote, em todas as amostras da série:

```text
suspension_position = float32(memória × 1000)
```

O `b` comum dessa conta é zero. O parser UDP não divide por mil; o `scale="1000.0"` do XML está em [Telemetry](telemetry.md).

## Experiments

### Baseline sem excursão

No rig `0x4b1abab0`, depois do sweep de esterço, 80 leituras em `6,4 s` (`SUSPENSION_BASE`): marcha `0`, `900` rpm, velocidade máxima `0,002` km/h, deslocamento do centro de massa `0`. A altura de `+0x270` no eixo Up, para os três blocos que têm posição, ficou constante (`−0,474`, `−0,487`, `−0,490` m). Nenhum `+0x10` variou. Não houve compressão nem mudança de contato. Suspensão e normal de contato, como nome de `+0x10`, continuam **INCONCLUSIVO**.

Janela seguinte: `28 s`, `397` amostras, velocidade máxima `0,006` km/h, variação máxima de `+0x270` no eixo Up de `0,1` mm. `+0x10` não saiu do lugar. Os vetores dessa série estão em [Wheels](wheels.md).

`+0x28`, o Z de `+0x20`, acompanha `Right.z` do chassi no par que essa medição chamava de dianteiro. Não é o curso. **REJEITADO**. Quais blocos eram esse par está em [Wheels](wheels.md).

### Captura ainda não executada, e o método

O desenho, antes de rodar: `scripts/research/capture_suspension_pair.py` só lê. Escolhe o processo cujo `/proc/pid/maps` contém `dirtrally2.exe`. A cada datagrama em UDP `20777` reabre a cadeia `dirtrally2.exe + 0x1681ce8` → `car + 0x30` → `container + 0x08` e confere `[rig + 0x12c0] == rig` e `[rig + 0x12d0] == 4`. Os escalares gravados são `rig + 0x1504`, `+0x1924`, `+0x1d44` e `+0x2164`, rotulados A–D. O pacote entra como RL, RR, FL, FR nos offsets `68`, `72`, `76` e `80`. Ponteiro nulo, falha de leitura ou troca de rig encerra a série.

`scripts/research/analyze_suspension_pair.py` testa as 24 permutações e as escalas `1`, `1000` e `0,001`, com um `b` só e um deslocamento de no máximo `8` amostras para a série inteira. A decisão usa T1–T6, e só se a amplitude do UDP passar de `10×` a de T0.

O processo só emite UDP depois de ler `hardware_settings_config.xml` com `udp enabled="true"` e `extradata="3"`. A sessão já aberta não relê esse arquivo. Ver [Telemetry](telemetry.md).

### Captura de 28 de setembro de 2026

Uma série, rig `0x4b76bab0`, 2065 datagramas em 99,9 s, mediana de 61,8 ms. A coluna de fase veio vazia. O campo de velocidade do pacote foi de `0,0001` a `11,07`. Cada canal de suspensão subiu e desceu centenas de vezes. Δ de sincronização escolhido: `0`.

Em todas as 2065 amostras, `float32(memória × 1000)` é bit a bit o `suspension_position` do pacote:

| UDP | Bloco | Escalar | Amplitude do UDP |
| :--- | :--- | :--- | ---: |
| RL | `+0x1680` | `rig + 0x1504` | 40,99 |
| RR | `+0x1aa0` | `rig + 0x1924` | 54,84 |
| FL | `+0x1ec0` | `rig + 0x1d44` | 42,49 |
| FR | `+0x22e0` | `rig + 0x2164` | 23,32 |

O `b` comum é zero. A segunda permutação, ainda com escala `1000`, tem correlação mínima `0,625` e RMSE `9,82`. A diferença de correlação é `0,375`. Não houve platô da memória com o UDP ainda em movimento.

## Confirmed

- Os quatro escalares, vezes `1000` em `float32`, são o `suspension_position` UDP na ordem RL, RR, FL, FR.
- Essa ordem fecha o mapa dos blocos. O rótulo antigo FL/FR/RL/RR trocava os eixos.
- `+0x1660 = base + [+0x1504] * Up`, com erro `4e-5` no rig `0x4b76bab0`. `+0x1670` é a base sem esse termo.
- `P` não é posição de mundo.

## Probable / Hypotheses

Nenhuma identificação extra do escalar além da igualdade com o UDP. O nome físico do ponto `+0x1660` não recebeu um rótulo estável no executável. A frase anterior, de que a base do ponto é o retorno da chamada e não um nome de contato, foi seguida pela fórmula fechada acima: a base é a rotação de `(L.x, L.y - c, L.z)` pelo quatérnion do chassi.

`h` como passo de tempo não está provado.

## Rejeitado

`+0x28` como curso de suspensão. É o Z de `+0x20`.

## Unknown

- Se `h` é o passo de tempo.
- Nome de `+0x1500`, de `+0x16cc` e de `+0x16d0`.
- Destino da chamada virtual em `0x140749292`.
- Qual par `i < 2` / `i >= 2` é o eixo dianteiro, olhando só esse desvio. A ordem dos cantos veio da captura, não desse teste.
- Ligação no código entre o store de `+0x1504` e o campo do datagrama. A igualdade é empírica.

## Discovery History

Hipótese inicial. `+0x1504` é um escalar por roda, como `suspension_position`, e a ligação entre eles não existe no código. `+0x1660` era um ponto montado como o retorno de `0x14072f8e0` mais `[+0x1504] * (+0x00)`, com o nome físico em aberto. `+0x28` parecia curso e foi rejeitado: é o Z de `+0x20`.

Evidência seguinte. O integrador em `0x1407376ed` atualiza `+0x1504` com `(N/h) * novo_+0x1500`. O escritor `0x140748670` fecha a conta com `L`, `c` e o quatérnion. O baseline parado não teve excursão.

Conclusão atual. Na série de 2065 datagramas, `float32(memória × 1000)` coincide bit a bit com o UDP, `b = 0`, e a permutação seguinte fica longe (`correlação mínima 0,625`, RMSE `9,82`). O mapa dos blocos é RL, RR, FL, FR.

## Related Systems

- [Wheels](wheels.md) — cabeçalho, `+0x10` e o uso de `+0x1660` como origem de `B`
- [Vehicle Setup](vehicle_setup.md) — `L` e `c`
- [Vehicle Transform](vehicle_transform.md) — quatérnion e a rotina de rotação
- [Vehicle Physics](vehicle_physics.md) — `T` e `V` em `+0x16d0`
- [Telemetry](telemetry.md) — offsets `68`–`80` e o `scale` que o parser não aplica
- [PhysicsRig](physics_rig.md)
