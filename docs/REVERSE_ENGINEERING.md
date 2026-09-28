# Engenharia Reversa da EGO Engine: DiRT Rally 2.0 (`dirtrally2.exe`)

Documento técnico de referência do **DR2 ModLoader v0.1.0** detalhando o mapeamento de subsistemas, estruturas de dados de física e motor em runtime, tabelas de símbolos, telemetria UDP, pontos de ancoragem e isolamento de rede Winsock.

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

A EGO Engine organiza seus módulos centrais através de descritores de subsistemas na seção `.data` e instâncias alocadas na memória dinâmica do heap durante o carregamento de fases/estágios.

### Ponteiros Globais da Sessão Ativa (.data)

- **Ponteiro Primário do Veículo Ativo (Sessão do Jogador):** `dirtrally2.exe + 0x1681ce8`
- **Ponteiro da Tabela de Veículos (Carro 1 / Player Rig):** `dirtrally2.exe + 0x15a4b00`
- **Ponteiro de Corrida Ativa (Race Instance):** `dirtrally2.exe + 0x15a9760`
- **Fallbacks Estáticos para Container de Veículo:** `dirtrally2.exe + 0x201b7c0` e `dirtrally2.exe + 0x20203a8`

---

## 3. Cadeia de Resolução de Ponteiros do Veículo

Para obter o estado cinemático do veículo ativo do jogador sem hooks intrusivos no loop de física, o DR2Hook percorre a cadeia determinística confirmada via inspeção em tempo real:

```
[dirtrally2.exe + 0x1681ce8]  (ou fallback +0x15a4b00 / +0x15a9760)
           │
           ▼
        Car*       (Heap: ex: 0x4a9d4b80 - "car 1")
           │
           │  +0x30 (Container de física do veículo)
           ▼
     Container*    (Heap: ex: 0x4aa0f100 - vtable dirtrally2.exe + 0x1400c30)
           │
           │  +0x08 (Physics Rig / DynamicsCarImpl concreto)
           ▼
    Physics Rig*   (Heap: ex: 0x4aa1bab0)
```

### Validação da Cadeia

1. **`gameBase`**: Obtido via `GetModuleHandleA(nullptr)` (RVA base `0x140000000`).
2. **`car`**: Leitura em `gameBase + 0x1681ce8`. Se nulo, recorre aos fallbacks `+0x15a4b00` ou `+0x15a9760`.
3. **`container`**: Leitura de 8 bytes em `car + 0x30`. Caso nulo, recorre a `gameBase + 0x201b7c0` ou `gameBase + 0x20203a8`.
4. **`physicsRig`**: Leitura de 8 bytes em `container + 0x08`. Aponta para a estrutura principal de integração cinemática.

---

## 4. Estrutura Interna do `Physics Rig` (`DynamicsCarImpl`)

O Physics Rig é o bloco alocado no heap onde residem as variáveis integradas pelo solver de física rígida a cada tick da simulação:

### Layout de Offsets Reais Mapeados

| Offset Relativo | Tipo | Descrição e Observações |
| :--- | :--- | :--- |
| `+0x2d0` | `Vector3` (SIMD 16B) | Posição física do centro de massa $\vec{p} = (x, y, z, 0)$ em metros |
| `+0x2e0` | `Vector4` (SIMD 16B) | Quatérnion de rotação $(q_x, q_y, q_z, q_w)$ |
| `+0x2f0` | `Vector3` (SIMD 16B) | Linha 0 da matriz de orientação (eixo lateral / right) |
| `+0x300` | `Vector3` (SIMD 16B) | Linha 1 da matriz de orientação (eixo vertical / up) |
| `+0x310` | `Vector3` (SIMD 16B) | Linha 2 da matriz de orientação (eixo frontal / forward) |
| `+0x320` | `Vector3` (SIMD 16B) | Velocidade linear $\vec{v} = (v_x, v_y, v_z, 0)$ em m/s |
| `+0x330` | `Vector3` (SIMD 16B) | Velocidade angular $\vec{\omega} = (\omega_x, \omega_y, \omega_z, 0)$ em rad/s |
| `+0x13d8` | `float` (4B) | **Velocidade angular do virabrequim** que o conta-giros usa, em rad/s. RPM = valor × 60 / (2π). No corte do Golf GTI 16v fica logo abaixo de `785.4` rad/s (`7500` RPM). |
| `+0x8e8` | `float` (4B) | Especificação estática da marcha lenta do motor, em RPM (ex.: `1080.0f`) |
| `+0x8f4` | `float` (4B) | Quantidade total de marchas à frente do veículo (ex.: `5.0f`) |
| `+0x918` | `float` (4B) | Rotação de potência máxima, em RPM (ex.: `5500.0f`). Não é o corte de giro. |
| `+0x1400` | `int32` (4B) | Número de marchas à frente usado pelo câmbio (ex.: `5`) |
| `+0x140c` | `float` (4B) | **Corte de giro** em rad/s. No GTI 16v é `785.398` rad/s, exatamente `7500` RPM. |
| `+0x1448` | `int32` (4B) | **Marcha engatada** (`0` = neutro, `1..n` = marchas à frente, `10` = ré) |
| `+0x1680` | `WheelRig` (`0x420` B) | Roda Dianteira Esquerda (Front-Left) |
| `+0x1aa0` | `WheelRig` (`0x420` B) | Roda Dianteira Direita (Front-Right) |
| `+0x1ec0` | `WheelRig` (`0x420` B) | Roda Traseira Esquerda (Rear-Left) |
| `+0x22e0` | `WheelRig` (`0x420` B) | Roda Traseira Direita (Rear-Right) |

### Sincronização Visual do Container

Além da posição física no `Physics Rig (+0x2d0)`, o container do veículo mantém a ancoragem visual da carroceria em `container + 0xcd0` (`x, y, z, yaw`), onde $y_{visual} \approx y_{fisico} - 0.44m$ em repouso estático sobre a suspensão.

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

---

## 8. Interceptação e Isolamento de Rede Winsock (`ws2_32.dll`)

Para garantir que o mod loader opere com segurança total contra trapaças em tabelas competitivas mundiais (RaceNet), o subsistema `NetworkGuard` atua diretamente na camada de transporte do sistema operacional via hooks MinHook na biblioteca `ws2_32.dll`.

### Rotinas Interceptadas

1. **`getaddrinfo`**:
   - **Assinatura:** `int WSAAPI HookedGetAddrInfo(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA *pHints, PADDRINFOA *ppResult)`
   - **Comportamento:** Inspeciona o nome do host alvo (`pNodeName`). Caso contenha substrings relacionadas aos serviços online do jogo (`codemasters`, `dirtgame`, `racenet`, etc.), a resolução é sumariamente abortada retornando `EAI_NONAME` (`11001` / `WSAHOST_NOT_FOUND`) e `ppResult = nullptr`.
2. **`connect`**:
   - **Assinatura:** `int WSAAPI HookedConnect(SOCKET s, const sockaddr *name, int namelen)`
   - **Comportamento:** Recusa a abertura de sockets TCP externos destinados aos servidores oficiais, definindo o erro do Winsock via `WSASetLastError(WSAECONNREFUSED)` (`10061`) e retornando `SOCKET_ERROR`.
3. **`sendto`**:
   - **Assinatura:** `int WSAAPI HookedSendTo(SOCKET s, const char *buf, int len, int flags, const sockaddr *to, int tolen)`
   - **Comportamento:** Bloqueia envio de datagramas UDP não locais com destino aos serviços de ranking, definindo `WSASetLastError(WSAEACCES)` (`10013`) e retornando `SOCKET_ERROR`.

