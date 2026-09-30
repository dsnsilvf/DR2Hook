# Wheels

## Overview

Quatro blocos de roda no `PhysicsRig`, passo `0x420`. A base estrutural e a diferença entre o cabeçalho em `rig + 0x1680` e o objeto em `rig + 0x1480` estão em [PhysicsRig](physics_rig.md). O curso em `+0x1504` está em [Suspension](suspension.md). O byte fora do bloco está em [Tyres](tyres.md).

A leitura ao vivo que abriu este mapa usou o mesmo processo e o mesmo `PhysicsRig` `0x4b1abab0`, em dois estados do mesmo carro: praticamente intacto e depois danificado, com a dianteira esquerda visualmente torta. O carro estava parado nos dois registros (marcha `0`, virabrequim perto de `900` RPM). Esterçamento, aceleração, frenagem, irregularidade e suspensão em movimento não foram medidos nessa primeira comparação. Nada disto está ligado na UI.

Classificação usada no inventário: **CONFIRMADO**, **PROVÁVEL**, **INCONCLUSIVO**, **REJEITADO**, **UNKNOWN**. Tabelas posteriores do mesmo fio usam **CONFIRMED**, **INCONCLUSIVE** e **UNKNOWN**. As duas grafias foram mantidas como estavam.

Conclusão atual da ordem dos blocos: RL, RR, FL, FR, fechada pela captura UDP. O rótulo antigo FL, FR, RL, RR trocava os eixos. O histórico está no fim deste arquivo.

## Known Structures

Cada cabeçalho guarda três `Vector3` SIMD de 16 bytes em `+0x00`, `+0x10` e `+0x20`, com padding `0` em `+0x0c`, `+0x1c` e `+0x2c`. Um quarto vetor, `+0x30`, fecha a base. `+0x10` não entra nessa base.

O subobjeto em `rig + 0x11a0` guarda a tabela 4×4. `[rig + 0x12c0]` é o próprio rig, `[rig + 0x12d0]` vale `4`, e os 16 floats estão em `rig + 0x12dc`. O nome do subobjeto fica em aberto: ele nasce no mesmo construtor que os blocos de `0x420` em `rig + 0x1480`, e só isso está provado.

## Memory Layout

Offsets do cabeçalho, relativos a `rig + 0x1680 + i·0x420`. A coluna do meio é o endereço no rig para `i = 0`, que o material cita ao carregar `+0x00` e `+0x10`.

| Offset no cabeçalho | Endereço no rig (`i = 0`) | Tipo | Descrição | Confiança |
| :--- | :--- | :--- | :--- | :--- |
| `+0x00` | `+0x1680` | `Vector3` 16 B | Up compartilhado, escrito igual nas quatro rodas. | CONFIRMADO |
| `+0x10` | `+0x1690` | `Vector3` 16 B | Direção por roda, no mundo. Na medição danificada o módulo é `1` em FL, FR e RL do rótulo da sessão, e `0.980` no RR dessa tabela. Normal do plano no quociente. Nome físico em aberto. | CONFIRMADO (vetor e papel) / UNKNOWN (física) |
| `+0x20` | `+0x16a0` | `Vector3` 16 B | Right. Na medição, o par então chamado de dianteiro compartilha um Right; o par então chamado de traseiro tem um Right próprio, próximo do Right do chassi. Esses nomes são os da sessão: ver a nota abaixo da tabela. | CONFIRMADO como medida; “eixo dianteiro” é o rótulo da época; causa da diferença de `2°–3°` INCONCLUSIVO |
| `+0x30` | `+0x16b0` | `Vector3` 16 B | `Right_roda × Up_roda`. Unitário, ortogonal a `+0x00` e a `+0x20`, `v2 × v0 · v3 = +1`. No par então chamado de dianteiro, coincide com o Forward do chassi. | CONFIRMADO |
| `+0x270` | não citado à parte | posição | Posição no mundo, a menos de dois metros do centro de massa. Três blocos fecham um retângulo. No bloco `+0x22e0` este campo estava zerado desde o estado intacto. No bloco `+0x1260`, o mesmo offset cai em `rig + 0x14d0`. | Medido; o mapa de cantos feito com ele é PROVÁVEL e não foi confirmado pelo esterço |

`+0x28` é o Z de `+0x20`. A hipótese de que fosse o curso de suspensão está em [Suspension](suspension.md) e foi REJEITADA.

`+0x20` “das dianteiras” e “das traseiras”, na classificação original, usa o rótulo da sessão: dianteiras = blocos `+0x1680` e `+0x1aa0` (então FL e FR); traseiras = `+0x1ec0` e `+0x22e0` (então RL e RR). A captura posterior troca os eixos desses rótulos. O texto não foi medido de novo depois disso. A frase original continua válida para esses blocos, não para o eixo que a ordem atual chama de dianteiro.

Estes três aparecem no inventário da roda sem uma segunda base escrita no texto. Não foram convertidos para endereço de rig.

| Offset citado | O que foi medido | Classe |
| :--- | :--- | :--- |
| `+0x78` | Mudou mais na FR e ficou congelado com o carro parado. | UNKNOWN |
| `+0x220` | `−0.712 / +0.712 / −0.712 / 0` nos dois estados. | REJEITADO como dano desta sessão. Ver [Damage](damage.md) |
| `+0x3d8` | Float só na FL, já no estado intacto (`≈ -0.28`, depois `≈ -0.26`). | REJEITADO como dano novo da FL. Ver [Damage](damage.md) |

| Bloco (ordem atual) | Escalar de suspensão, noutro documento |
| :--- | :--- |
| `+0x1680` RL | `rig + 0x1504` |
| `+0x1aa0` RR | `rig + 0x1924` |
| `+0x1ec0` FL | `rig + 0x1d44` |
| `+0x22e0` FR | `rig + 0x2164` |

A construção desse escalar e de `+0x1660` está em [Suspension](suspension.md). `B` na equação abaixo é derivado de `+0x1660`, não é o campo cru.

## Functions

| Endereço | Função | Relevância |
| :--- | :--- | :--- |
| `0x140739f44` / `0x140739f46` | `movups xmm, [rbx + rdi + 0x1690]` | Lê os 16 bytes de `Wheel + 0x10`. `rdi` é `[rsi + 0x120]`. `rbx` começa em zero e soma `0x420`. |
| `0x140739fb8` | Mesmo laço | Soma `0x420` a outro registrador e lê `+0x1680`, `+0x1684`, `+0x1688`, o vetor de `+0x00`. Contador em `[rsi + 0x130]`. |
| `0x140739700`–`0x14073a06f` | Função do leitor | `rsi` guarda o rig em `+0x120` e a contagem em `+0x130`. |
| `0x140739b91`–`0x140739bad` | Contador de três passos | Escolhe `+0x00`, `+0x20` ou `+0x30`. |
| `0x140739ea9` | `test r10d` | O quociente que usa `+0x10` só corre no passo `0`. |
| `0x1407398c0` | Laço que monta `P1` e `B` | Soma posição do chassi, retorno de `0x14072f8e0` e `[roda + 0x1504] * (+0x00)`. |
| `0x140739cea` | Chamada que não escreve `[rbp + 0x70]` | `P2` continua sendo a posição do chassi. |
| `0x140739bde` | Copia `[rbp - 0x70]` | Origem de `V0` a partir de `rig + 0x2c0`. |
| `0x140745e90` | Soma em `[rdi + 0x10]` e faz um produto sobre `T0` | Produz `V` e `T` usados na conta. |
| `0x140739d40` | Montagem de `q` | Tira a componente ao longo de `+0x00`. |
| `0x140739d47`–`0x140739fac` | Aritmética do quociente | Equação abaixo. Store em `0x140739faf` / `0x140739fac`. |
| `0x140739881` | Zera `xmm9` | Sem nova carga até o uso. |
| `0x14073e458`–`0x14073e560` | Único chamador do leitor | Não lê `+0x13c` antes do `ret`. |
| `0x14073b2dd` | Único `movups` que grava `0x1690` com passo `0x420` | Ponteiro do rig em `[r13 + 0x120]`, contagem em `[r13 + 0x130]`. Cópia cônica de `+0x1560`. |
| `0x14073a070`–`0x14073b352` | Função do cone | Chamada em `0x14073b68a` e em `0x14073e32a`. |
| `0x14073a2f1` | Grava `[rig + 0x300]` em `+0x1560` de cada roda | Único `movups`/`movaps` que grava `+0x1560`. O `jbe` seguinte está depois da gravação. |
| `0x14073b187` | Uso de `+0x1560` no cone | Citado com o store do Up. |
| `0x14073e32a` | Chamador | Roda o cone e depois chama o leitor em `0x14073e458`. |
| `0x140748670` | Por roda, escreve `+0x00`, `+0x20` e `+0x30` | Chamada em `0x14073e31b` e de novo em `0x14073e44d`. O intervalo que cairia em `+0x10` (`[rdi + 0x2c]`, com `rdi = rig + 0x1664`) não é gravado. Também escreve `+0x1660`. |
| `0x14072fd20` | Construtor do bloco de `0x420` | Zera o começo. Não escreve o offset que cai em `+0x1690` nem o offset `0x210` do bloco. |
| `0x140746b8a` | Construtor do subobjeto | `lea rcx, [rbx + 0x11a0]`. |
| `0x140edf760` | Rotina escalar de módulo | Usada no cone. |
| `0x1412b033c` | Float `0,173` | Limiar do cone. Cosseno de cerca de `80°`. |
| `0x141236634` | Float `0,05` | Passo com que o vetor fora do cone é puxado para `+0x00`. |
| `0x140708892` | Lê um float em `[rsi + 0x1690]` e compara com uma norma | Sem passo `0x420`. Não é o `WheelRig`. |
| `0x140b79ee6` | Grava `[rbp + 0x1690]` numa cópia de matriz | O objeto não é o rig das rodas. |
| `0x1409f7b8a` | Copia vetores entre `0x1a50` e `0x1ab0` | Sem o ponteiro do rig e sem o passo `0x420`. |
| `0x140e00412` | Compara um dword em `[rbx + 0x1690]` | Não é o vetor da roda. |
| `0x140baaef2` | `lea` de `0x1560` | `lea` falso. |
| `0x140738c20`, `0x14073ea9c` | Leitura de `0x1560` | Não são o store. |
| `0x140730420`, `0x14073099f`, `0x1407380a8` | Float solto em `+0x13c` | Não usam o índice `j + 4*i` nem o ponteiro do rig. |
| `0x14073d400`, `0x14073d840`, `0x14073be80`, `0x14073f220`, `0x14073f9c0` | Recebem `rig + 0x11a0` | Não leem o campo da tabela. Os `+0x13c` dentro de `0x14073be80` são `[rbp + 0x13c]` do frame. |
| `0x1407500b0` | Chamador de `0x14073f220` | Depois da tabela, segue para outros campos da roda sem reler `rig + 0x12dc`. |

Não há `rep movs` entre `0x140720000` e `0x140760000`. Não apareceu `memcpy` com tamanho `0x420` sobre o cabeçalho da roda. A busca por `movups` / `movaps` / `movdqu`, inclusive VEX, e por `lea` / `add` de `0x1690` e `0x1560`, não achou outra gravação do vetor além de `0x14073b2dd`.

## Data Flow

```text
[rig + 0x300]  Up do chassi
        │
        ▼
0x14073a2f1  →  +0x1560 de cada roda
        │
        ▼
0x14073b2dd  cone 0,173
        │
        ▼
+0x10  (rig + i·0x420 + 0x1690)
        │
        ▼
0x140739f46  como normal do plano
        │
        ▼
t em [subobjeto + 4*(j + 4*i) + 0x13c]
        = rig + 0x12dc, dezesseis floats
        │
        ▼
consumidor seguinte: desconhecido
```

O store do cone não explica o valor parado. Cada `+0x1560` ao vivo coincide com o `+0x10` da própria roda e difere do Up. Se o corpo do cone corresse até o fim com o carro parado, `+0x10` ficaria igual ao Up nas quatro, porque `dot(Up, +0x00)` já passa de `0,173`. A memória ao vivo não está assim. Essa função não é o escritor do valor estável enquanto o carro está parado. O escritor que mantém a diferença de `2°–3°` por roda não apareceu em outro `movups` / `movaps` para `0x1690`.

Para cada roda `i`, a gravação achada é:

```text
S  = [rig + i*0x420 + 0x1560]
up = [rig + i*0x420 + 0x1680]
se dot(S, up) >= 0,173:
    [rig + i*0x420 + 0x1690] = S
senão:
    repetir S = (xmm15 / |S|) * (S + 0,05 * up)
    até dot(S, up) >= 0,173
    gravar S em +0x10
```

Não há produto com ângulo de esterço nessa gravação.

`0x140748670` escreve `+0x00`, `+0x20` e `+0x30`. Não grava `+0x10`.

## Mathematics

### Os três vetores não são uma base

Medidos com o carro danificado parado. `v0 = +0x00`, `v1 = +0x10`, `v2 = +0x20`.

| Teste | FL | FR | RL | RR |
| :--- | :--- | :--- | :--- | :--- |
| `\|v0\|`, `\|v1\|`, `\|v2\|` | `1, 1, 1` | `1, 1, 1` | `1, 1, 1` | `1, 0.980, 1` |
| `v0 · v1` | `+0.905` | `+0.983` | `+0.951` | `+0.964` |
| `v0 · v2` | `0` | `0` | `0` | `0` |
| `v1 · v2` | `+0.395` | `−0.183` | `+0.308` | `−0.058` |
| `v0 · (v1 × v2)` | `+0.161` | `−0.009` | `+0.002` | `−0.165` |

Os rótulos FL/FR/RL/RR desta tabela, e os da inclinação mais abaixo, são os da sessão, anteriores à troca de eixos. Naquele rótulo, FL é o bloco `+0x1680`, FR é `+0x1aa0`, RL é `+0x1ec0` e RR é `+0x22e0`. Os números não foram reetiquetados para a ordem atual (RL, RR, FL, FR).

`v0` é o mesmo nas quatro rodas. O ângulo com Up do chassi é cerca de `0.2°`. `v2` de FL e de FR é o mesmo vetor, também a cerca de `0.2°` de Right. `v2 × v0` coincide com Forward nas dianteiras. RL e RR ficam a `2.3°` e `3.2°` de Right, cada uma com o seu `v2`, e continuam ortogonais a `v0`.

`v1` está no mesmo espaço: o módulo das componentes em Right/Up/Forward é o módulo do vetor. Ele não é o eixo que falta. Esse eixo já sai de `v2 × v0` e é Forward. `v1 ·` esse eixo fica entre `−0.17` e `+0.17`, longe de `±1`.

Por isso `+0x00`, `+0x10` e `+0x20` não são uma matriz ortonormal nem uma `WheelLocalRotation`. A fórmula `WheelLocalRotation × ChassisRotation = WheelWorldRotation` não tem os três fatores nestes offsets. O que há é um par de eixos do chassi escrito em cada bloco, mais um terceiro vetor independente, no mundo.

O espaço é o de Right/Up/Forward do rig: eixos do corpo, base destra, `Right × Up = Forward`. Detalhe da base em [Vehicle Transform](vehicle_transform.md).

### Quociente e o parâmetro `t`

A primeira leitura do quociente, antes de fechar os registradores, era:

```text
(dot(+0x10, q) + c) / (dot(+0x10, +0x00) + c)
```

`q` é um vetor do qual a componente ao longo de `+0x00` já foi retirada. O jogo usa `+0x10` como direção dentro dessa projeção. Não lê o campo como ângulo, flag ou dano.

Com `xmm9` zerado em `0x140739881` e sem nova carga até o uso, o termo somado some. A conta que o leitor executa é `dot(+0x10, q) / dot(+0x10, +0x00)`. A forma fechada, com `t`, está mais abaixo.

Há dois laços aninhados, ambos com passo `0x420`. O índice externo é `r12` (base `rbx`). O interno é `r8` (base `rcx`). Um contador `ecx`, de `0` a `2`, escolhe o eixo da roda externa antes do laço interno:

| Passo | Eixo carregado |
| :--- | :--- |
| `0` | `+0x00` em `+0x1680` |
| `1` | `+0x20` em `+0x16a0` |
| `2` | `+0x30` em `+0x16b0` |

No mesmo bloco, o contador de três passos também escolhe:

| Passo | Offset no cabeçalho | Vetor medido |
| :--- | :--- | :--- |
| 0 | `+0x00` | Up compartilhado |
| 1 | `+0x20` | Right |
| 2 | `+0x30` | o produto `Right_roda × Up_roda` |

Nos três passos o laço interno grava três outros escalares, um por eixo, noutro destino (`[r9]`). Esses três não são a tabela `+0x13c`. O quociente que usa `+0x10` só corre no passo `0`.

Nomes provisórios, no momento em que o laço interno lê os slots:

| Símbolo | Slot | Origem provada |
| :--- | :--- | :--- |
| `P2` | `[rbp + 0x70]` | Posição do chassi, `rig + 0x2d0`. A chamada em `0x140739cea` não escreve esse slot. |
| `V0` | cópia inicial de `[rbp + 0x60]` | `rig + 0x2c0`, copiado de `[rbp - 0x70]` em `0x140739bde`. |
| `V` | `[rbp + 0x60]` depois da chamada | `V0` mais o vetor que `0x140745e90` soma em `[rdi + 0x10]`. |
| `T0` | cópia inicial de `[rbp + 0x50]` | Primeiros 16 bytes de `rig + 0x2b0`. |
| `T` | `[rbp + 0x50]` depois da chamada | `T0` mais um produto feito por `0x140745e90`. |
| `P1` | `[rbp + rdx + 0x120]` | Por roda interna. O laço em `0x1407398c0` soma a posição do chassi, o vetor devolvido por `0x14072f8e0` e `[roda + 0x1504] * (+0x00)`. |
| `B` | `[rbp + rdx + 0x160]` | Três floats gravados nesse mesmo laço anterior, a partir de `[roda + 0x1660]`. |
| `s` | `xmm8` | `[rig + 0x370] * 0,0001`. Ao vivo, `0x370` vale `1083,53` e `s` vale `0,10835`. |
| `up_j` | `[rig + j*0x420 + 0x1680]` | `+0x00` da roda interna. |
| `v_i` | `[rig + i*0x420 + 0x1690]` | `+0x10` da roda externa. |

`xmm9` continua zero. A conta, com os sinais do assembly entre `0x140739d47` e `0x140739fac`:

```text
d  = P1 - P2
W  = T - cross(d, V)
q0 = (W - B) / s
q  = q0 - dot(q0, up_j) * up_j
resultado = dot(v_i, q) / dot(v_i, up_j)
```

O resultado vai para `[rsi + 4*(j + 4*i) + 0x13c]`. Com quatro rodas isso é uma matriz 4×4: a linha é a roda externa, a coluna é a roda interna. No rig, a tabela é `rig + 0x12dc`. Valores parados na ordem de `1e-4`.

`q` é ortogonal a `up_j` porque a última subtração tira essa componente. Com `up_j` unitário, a reta `X = t * up_j` e o plano `dot(v_i, X) = dot(v_i, q)` se cruzam em:

```text
t = dot(v_i, q) / dot(v_i, up_j)
```

Esse `t` é o escalar gravado. A álgebra é a de uma interseção reta/plano. O plano passa pela ponta de `q` e tem normal `v_i`. A reta passa pela origem e segue `+0x00` da roda interna. O nome físico do plano continua desconhecido: a fórmula não diz se `v_i` é normal de contato, eixo de força ou outra direção.

No carro parado, `dot(+0x10, +0x00)` vale `0,99865`, `0,99932`, `0,99914` e `0,99896` nos quatro blocos. É o cosseno do ângulo de `2,1°` a `3,0°` entre os dois unitários. O denominador está perto de `1`.

`+0x00`, `+0x20` e `+0x30` são os três eixos que o contador escolhe como direção da reta, cada um num passo. `+0x10` não é o quarto eixo dessa base. No passo `0` ele entra só como a normal do plano.

`B` não é o campo cru. O laço anterior carrega `[roda + 0x1660]` e grava em `[rbp + j*0x10 + 0x160]` o resultado da mesma família de conta. Quem escreve o campo é `0x140748670`: o vetor devolvido por `0x14072f8e0`, mais `[roda + 0x1504] * (+0x00)`. É a mesma forma de `P1`. A forma fechada desse ponto, posterior a esta descrição, está em [Suspension](suspension.md). O nome físico, na época desta equação, não estava fechado.

`T` e `V` estão em [Vehicle Physics](vehicle_physics.md). A rotação `0x14072f8e0` está em [Vehicle Transform](vehicle_transform.md).

### O que `+0x10` fez entre os dois estados

Inclinação de `v1` em relação a `+Y` do mundo. Rótulos da sessão, anteriores à troca de eixos:

| Roda | Intacto | Danificado |
| :--- | :--- | :--- |
| FL | `5.0°` | `34.9°` |
| FR | `4.1°` | `4.0°` |
| RL | `5.5°` | `29.0°` |
| RR | `4.1°` | `19.4°` |

No estado danificado, o Up do chassi já está a `11.7°` de `+Y`. O ângulo de `v1` com esse Up é `25.0°`, `10.7°`, `17.7°` e `10.4°`. A FR continua perto de `+Y` do mundo e mais perto dele do que do Up do chassi.

Entre os dois estados, `Right.z` do chassi foi de cerca de `+0.38` para `−0.32`. O `v1` da FR no mundo ficou em `≈ (−0.016, +0.998, −0.069)` nos dois registros. Ele não girou com o corpo. FL, RL e RR mudaram no mundo. Uma roda danificada não move só o próprio `+0x10`, e o vetor também não é uma cópia da atitude do chassi.

Componentes de `v1` no frame do chassi, `(Right, Up, Forward)`, no estado danificado:

| Roda | Right | Up | Forward |
| :--- | :--- | :--- | :--- |
| FL | `+0.392` | `+0.906` | `+0.159` |
| FR | `−0.186` | `+0.982` | `−0.011` |
| RL | `+0.304` | `+0.952` | `+0.013` |
| RR | `−0.052` | `+0.964` | `−0.170` |

O ângulo no plano Right/Forward fica instável quando a parte horizontal é pequena. Na FR essa parte vale `0.19`, então um `atan2` não é um ângulo de direção utilizável.

## Experiments

### Posição `+0x270` e o mapa que o esterço não confirmou

Três blocos fecham um retângulo: bitola `1,42 m`, distância entre eixos `2,472 m`. O centro de massa está mais perto do eixo em que a componente Forward vale `+0,93 m` do que do eixo em `−1,54 m`. Com `Right × Up = Forward`, o mapa provável, ainda com o rótulo histórico:

| Bloco | Rótulo usado até aqui | Posição no chassi | Inclinação de `+0x10` em relação a `+Y` |
| :--- | :--- | :--- | :--- |
| `+0x1680` | FL | esquerda, eixo distante (traseiro) | `5,0° → 34,9°` |
| `+0x1aa0` | FR | direita, eixo próximo (dianteiro) | `4,1° → 4,0°`, variação mundial `0,008` |
| `+0x1ec0` | RL | esquerda, eixo próximo (dianteiro) | `5,5° → 29,0°` |
| `+0x22e0` | RR | `+0x270` zerado desde o estado intacto | `4,1° → 19,4°` |

O canto que falta no retângulo, direita no eixo distante, está em `rig + 0x14d0`, num bloco `+0x1260` que não tem o mesmo `+0x10`. A troca dianteira/traseira acima é **PROVÁVEL** pela geometria de `+0x270`. O esterçamento parado não moveu `+0x10`, então não confirma esse mapa.

A componente Right de `+0x10` tem sinal oposto à posição lateral nos três blocos que têm `+0x270`. O alinhamento com a direção horizontal que aponta para o centro fica entre `0,57` e `0,73`. O produto escalar de `+0x10` com o vetor do centro de massa até `+0x270` fica perto de `−0,52` nos três, no mesmo sentido em que a roda está abaixo do centro de massa e `+0x10` aponta para cima. Isso não identifica `+0x10` com o raio até a roda.

A roda cuja posição é dianteira direita manteve o vetor mundial enquanto o Up do chassi foi a `11,7°` de `+Y`. Uma direção presa ao corpo teria ido junto. As outras três mudanças não são a imagem de uma única rotação do chassi: se fossem, as quatro teriam se movido. O que resta é uma direção por roda, no mundo, quase vertical quando o ponto não se alterou, usada pelo laço como normal de um produto escalar. Normal de contato, eixo vertical de suspensão e qualquer outra direção de mundo que descanse para cima continuam compatíveis com essa conta. O esterço parado é o teste que separa essas leituras.

Na sessão seguinte, mesmo PID e mesmo `PhysicsRig` `0x4b1abab0`, etapa nova e carro parado, `+0x10` foi lido em três posições do volante: fim à direita, centro e fim à esquerda. Os quatro vetores saíram bit a bit iguais nos três estados. O Up do chassi variou menos de `0,0002`. Com o carro parado, `+0x10` não responde ao esterçamento.

### Baseline sem excursão

Logo depois desse sweep, 80 leituras em `6,4 s` formam `SUSPENSION_BASE`. Marcha `0`, `900` rpm, velocidade máxima `0,002` km/h, deslocamento do centro de massa `0`. Nenhum `+0x10` variou. A altura de `+0x270` no eixo Up, para os três blocos que têm posição, também ficou constante (`−0,474`, `−0,487`, `−0,490` m). Não houve compressão nem mudança de contato durante a série, então suspensão e normal de contato continuam **INCONCLUSIVO**.

Uma janela seguinte de `28 s` e `397` amostras, ainda no mesmo rig, teve velocidade máxima `0,006` km/h e variação máxima de `+0x270` no eixo Up de `0,1` mm. `+0x10` não saiu do lugar. Não houve excursão suficiente para testar suspensão contra contato.

`SUSPENSION_BASE`, ângulo de `+0x10` com `+Y` e com o Up:

| Bloco | `+0x10` | `+Y` | Up |
| :--- | :--- | :--- | :--- |
| `+0x1680` | `+0,03402, +0,99817, +0,05009` | `3,47°` | `2,97°` |
| `+0x1AA0` | `+0,02729, +0,99895, −0,03681` | `2,63°` | `2,11°` |
| `+0x1EC0` | `+0,03084, +0,99872, +0,03985` | `2,89°` | `2,36°` |
| `+0x22E0` | `+0,02120, +0,99872, −0,04594` | `2,90°` | `2,61°` |

Os componentes `(+0,03402, +0,99817, +0,05009)` e os três vizinhos são a captura parada de `+0x10`, não de `+0x00`. `+0x00` é o Up compartilhado.

O curso que depois se moveu de verdade, e a ordem UDP, estão em [Suspension](suspension.md). Nenhum campo UDP tem o formato de `+0x10`.

## Confirmed

| Afirmação | Classe |
| :--- | :--- |
| `+0x00`, `+0x10`, `+0x20` são vetores no mesmo espaço de Right/Up/Forward do `PhysicsRig` | CONFIRMADO |
| Esse espaço é o do mundo: eixos do corpo, base destra, `Right × Up = Forward` | CONFIRMADO |
| `+0x00` é o Up compartilhado, escrito igual nas quatro rodas | CONFIRMADO |
| `+0x20` do par então chamado de dianteiro (`+0x1680` e `+0x1aa0`) é um Right compartilhado. A frase original diz “eixo dianteiro” | CONFIRMADO como medida, com esse rótulo |
| `+0x20` do par então chamado de traseiro (`+0x1ec0` e `+0x22e0`) é um Right próprio de cada roda, próximo do Right do chassi | CONFIRMADO como medida; causa da diferença de `2°–3°` INCONCLUSIVO |
| `+0x30` é o terceiro eixo da base, `Right_roda × Up_roda` | CONFIRMADO |
| `+0x10` é um vetor de direção por roda, no mundo, de módulo `1` em FL, FR e RL na medição danificada. Esses três nomes são o rótulo da sessão; RR dessa tabela, bloco então `+0x22e0`, mediu `0.980` | CONFIRMADO |
| `+0x10` é a normal do plano no quociente. Nome físico em aberto. | CONFIRMED (papel na conta) / UNKNOWN (física) |
| `q = (W - B) / s` com a componente em `+0x00` removida. `W = T - cross(P1 - P2, V)` | CONFIRMED |
| `+0x13c` é o parâmetro `t` da reta ao longo de `+0x00` com o plano normal a `+0x10` que passa por `q` | CONFIRMED |
| A tabela 4×4 está em `rig + 0x12dc`, dezesseis `t`, valores parados na ordem de `1e-4` | CONFIRMED (conteúdo) / UNKNOWN (sistema) |
| `+0x20` e `+0x30` são os outros dois eixos do contador, cada um a direção da reta num passo seguinte, com outra saída | CONFIRMED |
| A relação algébrica: `+0x10` é a normal; `+0x00` é a direção da reta e o eixo tirado de `q` | CONFIRMED |
| `+0x1660` é montado como o retorno de `0x14072f8e0` mais `[+0x1504] * (+0x00)`; `B` é o que o laço deriva desse campo | CONFIRMED (construção) / UNKNOWN (nome), antes do fechamento em [Suspension](suspension.md) |
| `V` é `rig + 0x2c0`, copiado de `rig + 0x180` | CONFIRMED (cópia) / UNKNOWN (nome) |
| Ordem dos quatro blocos RL, RR, FL, FR | Fechada pela captura em [Suspension](suspension.md). Não há tabela nativa no executável com esses nomes |

`+0x1560` é a fonte `S` do cone. O único store achado grava o Up do chassi. O valor ao vivo, igual a `+0x10`, não saiu desse store. Classe: CONFIRMED (papel) / UNKNOWN (valor parado).

## Probable / Hypotheses

| Afirmação | Classe |
| :--- | :--- |
| Troca dianteira/traseira dos rótulos históricos, pela geometria de `+0x270` | PROVÁVEL. O esterço parado não confirmou. A captura UDP fechou a ordem por outro caminho; ver o histórico |
| Nome físico estável de `+0x10`: direção de esterço, câmber, normal de contato ou eixo do montante | INCONCLUSIVO |
| `+0x10` muda com suspensão ou com a normal de contato | INCONCLUSIVO. O baseline foi lido; a excursão não ocorreu |
| Causa da diferença de `2°–3°` entre o Right traseiro e o Right do chassi | INCONCLUSIVO |
| Significado de `T` (`rig + 0x2b0`) | INCONCLUSIVE. Store direto não achado |
| Sistema que consome a tabela 4×4 | UNKNOWN no inventário posterior: CONFIRMED o conteúdo, UNKNOWN o sistema |

## Rejeitado

| Afirmação | Classe |
| :--- | :--- |
| `+0x28` como curso de suspensão. É o Z de `+0x20`; no par então chamado de dianteiro acompanha `Right.z` do chassi | REJEITADO |
| Os três vetores formam a orientação completa da roda | REJEITADO |
| `+0x10` é o terceiro eixo dessa orientação | REJEITADO |
| `+0x10` é o canal `wheel_misalign_angle_damage` | REJEITADO |
| `+0x10` muda só na roda visualmente torta | REJEITADO |
| `+0x10` é só a atitude do chassi reescrita | REJEITADO |
| `+0x10` muda com o esterçamento, carro parado. Direita, centro e esquerda deram o mesmo vetor nos quatro blocos | REJEITADO |
| Rótulo antigo `+0x1680` FL, `+0x1aa0` FR, `+0x1ec0` RL, `+0x22e0` RR | Superado. A captura mostrou que esse rótulo trocava os eixos. Não é a ordem atual |

`+0x3d8` e `+0x220` como dano desta sessão estão em [Damage](damage.md).

## Unknown

| Item | Conclusão | Classe |
| :--- | :--- | :--- |
| Nome físico de `+0x10` | Normal do plano na conta do `t`. Nome em aberto. | UNKNOWN (física) |
| Origem do valor inclinado de `+0x10` | Única gravação achada é a cópia cônica de `+0x1560`. Ela não produz o valor parado. Sem `rep movs` e sem store em `[subobjeto + 0x210]`. | UNKNOWN |
| Origem do valor parado de `+0x1560` | Único store: Up do chassi. Ao vivo o campo coincide com o `+0x10` inclinado. | UNKNOWN |
| Consumidor de `+0x13c` | Tabela confirmada em `rig + 0x12dc`. Nenhum leitor do índice `j + 4*i`. A varredura do `.text` achou só a gravação. Não há `lea` nem `add` de `0x13c` nesse objeto. | UNKNOWN |
| Origem dos `2°–3°` | Não é o cone nem o store do Up. Sem `memcpy` de `0x420` nesse campo. `dot` ao vivo `0,9986`–`0,9993`. | UNKNOWN |
| `wheel_rotation_rate` / `wheel_speed` | Nomes no executável. Offset interno não isolado. UDP não capturado para eles. | UNKNOWN |
| `+0x78` | Mudou mais na FR e ficou congelado com o carro parado. | UNKNOWN |
| Nome do subobjeto em `rig + 0x11a0` | Nasce no mesmo construtor que os blocos de `0x420`. | Em aberto |
| Bloco `+0x1260` | Tem um `+0x270` em `rig + 0x14d0` e não tem o mesmo `+0x10`. | Sem papel atribuído |

## Discovery History

### Ordem dos blocos

Hipótese inicial, no inventário, classificada então como CONFIRMADO: `+0x1680` FL, `+0x1aa0` FR, `+0x1ec0` RL, `+0x22e0` RR. Nesse ponto o texto diz que o rótulo da tabela do SSOT continuava sendo essa ordem histórica.

Evidência seguinte. `+0x270` de três blocos forma um retângulo. A troca dianteira/traseira em relação a esse rótulo foi marcada PROVÁVEL. O bloco `+0x1aa0` caiu, nessa leitura, no eixo próximo (dianteiro). `+0x22e0` tinha `+0x270` zerado. O esterço parado não moveu `+0x10` e não confirmou o mapa. Não há tabela nativa que ordene `0x1680`, `0x1aa0`, `0x1ec0`, `0x22e0` como RL/RR/FL/FR. A ordem UDP e a tabela de dano coincidem entre si e também são pares de eixo, mas isso, sozinho, não ligava os blocos.

Conclusão atual. A captura de 28 de setembro de 2026, em [Suspension](suspension.md), fecha o mapa: `+0x1680` RL, `+0x1aa0` RR, `+0x1ec0` FL, `+0x22e0` FR. O rótulo antigo trocava os eixos. O layout do início do arquivo monolítico já usa esta ordem.

O mapa PROVÁVEL de `+0x270` não foi reclassificado pelo material. Ele colocava `+0x1aa0` no eixo próximo (dianteiro). A ordem aceita coloca esse bloco em RR. As duas leituras ficam como estão: a ordem dos cantos aceita é a da captura; a atribuição de eixo feita com `+0x270` não chegou a ser confirmada e não foi declarada rejeitada. O retângulo posterior de `L` em `+0x1480` (`z = 0,935` dianteiro e `-1,538` traseiro) usa a ordem da captura e está em [Vehicle Setup](vehicle_setup.md).

### Onde fica o escalar `+0x13c`

Primeira leitura: `rsi` é um objeto que guarda o rig em `+0x120`, e o escalar não fica no `PhysicsRig`.

Evidência seguinte: o construtor grava o próprio rig em `[rig + 0x12c0]` e a contagem `4` em `[rig + 0x12d0]`. Os 16 floats estão em `rig + 0x12dc`.

Conclusão atual: a tabela é esse bloco, dentro do rig. O consumidor continua desconhecido.

### O que `+0x10` é

Primeira leitura: três vetores candidatos a orientação da roda. Rejeitado. Também rejeitado como canal de misalign, como atitude do chassi e como resposta ao esterço parado.

Conclusão atual: direção por roda no mundo, usada como normal do plano no `t`. O nome físico permanece em aberto. O valor parado, `2°–3°` fora de `+0x00`, não sai do único store achado.

## Related Systems

- [Suspension](suspension.md) — `+0x1504`, `+0x1660`, ordem UDP
- [Vehicle Setup](vehicle_setup.md) — ponto `L` que o escritor gira para montar `+0x1660`
- [Vehicle Transform](vehicle_transform.md) — base do chassi e `0x14072f8e0`
- [Vehicle Physics](vehicle_physics.md) — `T`, `V`, `+0x370`
- [Tyres](tyres.md) — byte fora do bloco
- [Damage](damage.md) — canais nomeados; `+0x10` não é um deles
- [Telemetry](telemetry.md) — o pacote não traz este vetor
- [PhysicsRig](physics_rig.md)
