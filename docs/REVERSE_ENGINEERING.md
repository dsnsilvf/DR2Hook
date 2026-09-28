# Engenharia Reversa da EGO Engine: DiRT Rally 2.0 (`dirtrally2.exe`)

Documento técnico de referência do DR2Hook detalhando o mapeamento de subsistemas, estruturas de dados de física em runtime, tabelas de símbolos, telemetria UDP e pontos de ancoragem na memória do executável.

---

## 1. Visão Geral do Alvo e Arquitetura do Executável

- **Binário Alvo:** `dirtrally2.exe` (Steam / Codemasters)
- **Arquitetura:** x86-64 (AMD64), Little-Endian
- **Base de Imagem PE Padrão:** `0x140000000`
- **Compilador Original:** Microsoft Visual C++ (MSVC x64) com otimizações de link-time code generation (`/LTCG`) e vetorização SIMD (AVX / SSE2).
- **Engine Gráfica e Física:** EGO Engine 4.x / Codemasters neon architecture.

### Estrutura de Seções PE

| Seção | Endereço Virtual Relativo (RVA) | Permissões | Conteúdo e Propósito |
| :--- | :--- | :--- | :--- |
| `.text` | `+0x00001000` | `RX` (Leitura / Execução) | Código de máquina do motor, rotinas de física e sub-rotinas de tick. |
| `.rdata` | `+0x00A00000` | `R` (Somente Leitura) | Tabelas de métodos virtuais (`vtables`), descritores RTTI MSVC, constantes de ponto flutuante e strings estáticas de depuração. |
| `.data` | `+0x01600000` | `RW` (Leitura / Escrita) | Tabela global de subsistemas da engine, ponteiros singletons e estados mutáveis de sessão. |
| `_RDATA` | Variável | `R` (Somente Leitura) | Metadados suplementares da CRT e tabelas de inicialização de exceção (`pdata`/`xdata`). |

---

## 2. Arquitetura de Subsistemas da EGO Engine e `VehicleManager`

A EGO Engine organiza seus módulos centrais através de um registro de subsistemas em blocos contíguos de `0x80` bytes alocados na seção `.data`. Cada bloco contém o identificador do subsistema em ASCII seguido de ponteiros de ciclo de vida e da instância ativa no heap.

### Layout do Descritor de Subsistema (`0x80` bytes)

```text
[Offset 0x00] Descritor ASCII (até 32 bytes): "vehicle_manager\0"
[Offset 0x20] uintptr_t pInstance -> Ponteiro global para a instância no heap
[Offset 0x28] uintptr_t pVtable / LifecycleHandler
[Offset 0x30 ... 0x7F] Metadados internos de inicialização e dependências
```

### Endereços Globais Mapeados

- **Descritor `"vehicle_manager"`:** `dirtrally2.exe + 0x168c0e0` (`0x14168c0e0`)
- **Ponteiro da Instância `VehicleManager*`:** `dirtrally2.exe + 0x168c100` (`0x14168c100`)
- **Padrão AOB de Assinatura para Pattern Scanning:**
  `76 65 68 69 63 6C 65 5F 6D 61 6E 61 67 65 72` (ASCII: `"vehicle_manager"`)

---

## 3. Cadeia de Resolução de Ponteiros do Veículo

Para obter o estado cinemático do veículo ativo do jogador sem hooks intrusivos no loop de física, o DR2Hook percorre a cadeia determinística de ponteiros:

```
[dirtrally2.exe + 0x168c100]
           │
           ▼
    VehicleManager*  (Heap)
           │
           │  +0x30 (Veículo ativo do jogador)
           ▼
     DynamicsCar*    (Heap, Interface polimórfica)
           │  Vtable: 0x1412af930 (dirtrally2.exe + 0x12af930)
           │
           │  +0x08 (Implementação concreta de física)
           ▼
   DynamicsCarImpl*  (Heap, Bloco alinhado de 0x3130 bytes)
```

### Validação da Cadeia

1. **`gameBase`**: Obtido via `GetModuleHandleA(nullptr)`.
2. **`VehicleManager* mgr`**: Leitura de 8 bytes em `gameBase + 0x168c100`. Se nulo ou não mapeado, indica que a engine ainda está em bootstrapping.
3. **`DynamicsCar* car`**: Leitura de 8 bytes em `mgr + 0x30`.
   - Vtable esperada no offset `0x00`: `gameBase + 0x12af930` (`0x1412af930`).
   - Se nulo, o jogador está nos menus ou tela de carregamento.
4. **`DynamicsCarImpl* impl`**: Leitura de 8 bytes em `car + 0x08`.
   - Se válido, representa a simulação física do chassi e suspensões.

---

## 4. Estrutura Interna do `DynamicsCarImpl` (Física Rígida)

O `DynamicsCarImpl` é uma estrutura de `0x3130` bytes (alinhada a 16 bytes para compatibilidade com registros XMM/SIMD) responsável pela integração de equações de movimento rígido e dinâmica de suspensão.

### Layout de Offsets Relevantes

| Offset Relativo | Tipo | Descrição |
| :--- | :--- | :--- |
| `+0x100` | `WheelState` (`0x90` B) | Subestrutura da Roda 0: Dianteira Esquerda (Front Left - FL) |
| `+0x190` | `WheelState` (`0x90` B) | Subestrutura da Roda 1: Dianteira Direita (Front Right - FR) |
| `+0x220` | `WheelState` (`0x90` B) | Subestrutura da Roda 2: Traseira Esquerda (Rear Left - RL) |
| `+0x2b0` | `WheelState` (`0x90` B) | Subestrutura da Roda 3: Traseira Direita (Rear Right - RR) |
| `+0x2e0` | `Vector3` (SIMD 16B) | Velocidade Linear $\vec{v} = (v_x, v_y, v_z, 0)$ em m/s |
| `+0x300` | `Vector3` (SIMD 16B) | Posição Global no Mundo $\vec{p} = (p_x, p_y, p_z, 1)$ em metros |
| `+0x310` | `Matrix3x3` / Quat | Matriz de Rotação / Orientação angular do chassi |
| `+0x340` | `Vector3` (SIMD 16B) | Velocidade Angular $\vec{\omega} = (\omega_x, \omega_y, \omega_z, 0)$ em rad/s |

### Detalhes das Subestruturas de Roda (`WheelState` - `0x90` bytes)

Cada subestrutura de roda possui os seguintes campos críticos para estabilização cinemática:
- `+0x00`: Compressão da mola de suspensão (`float suspensionCompression`, 0.0 = totalmente distendida/airborne, 1.0 = final de curso/batente). No repouso estático sob 1G de gravidade, equilibra-se em $\approx 0.35f$.
- `+0x04`: Velocidade angular da rotação da roda (`float angularVelocity` em rad/s).
- `+0x08`: Flag booleana de contato (`uint32_t inContact`, 1 se o pneu estiver em aderência/contato com a malha da pista).

---

## 5. RTTI Mapeada (Run-Time Type Information)

A busca por descritores RTTI em `.rdata` confirma as hierarquias polimórficas da EGO Engine:

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

Esses símbolos confirmam que `DynamicsCar` é uma interface abstrata exposta para subsistemas externos (câmera, áudio, replay), enquanto `DynamicsCarImpl` é a classe concreta de cálculo numérico de dinâmica vehicular.

---

## 6. Subsistema de Telemetria Nativa UDP

O jogo suporta telemetria via pacotes de rede UDP configurados em `hardware_settings_config.xml` ou `DRSM_data.xml`:

- **Porta Padrão:** UDP 20777 (configurável, ex.: 20778).
- **Mecanismo:** Chamadas contínuas a `sendto()` do Winsock (`ws2_32.dll`) no fim de cada tick de simulação.
- **Formato dos Dados:** Estrutura binária contendo `total_time`, `lap_time`, `lap_distance`, velocidade vetorial, coordenadas $x, y, z$, forças G ($g_x, g_y, g_z$), rotação dos 4 pneus e curso das 4 suspensões.
- **Comparação DR2Hook vs. Telemetria UDP:**
  - *UDP:* Unidirecional (somente leitura), taxa de atualização discreta sujeita a buffer e latência de rede.
  - *DR2Hook Direct Memory:* Bidirecional (leitura e escrita), latência zero (acesso síncrono no mesmo processo) e permite savestate, restore e alteração de física.

---

## 7. Comandos Internos de Depuração da Engine

No executável foram identificadas strings associadas ao sistema de comandos do console interno:

1. `"debug.global.teleport.pressed"`: Comando de trigger interno utilizado pelos desenvolvedores para reposicionar veículos durante testes de colisão e malha.
2. `"reset_vehicle"`: Rotina padrão de recuperação que reposiciona o veículo na pista aplicando penalidade de tempo de jogo.
3. `"vehicle_manager"`: Nome canônico registrado na tabela central de subsistemas em `.data`.
