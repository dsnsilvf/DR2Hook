# DR2 ModLoader v0.1.0 — Mod Loader & Hook Framework para DiRT Rally 2.0

> **Framework de modding extensível e voltado para treino, pesquisa de telemetria, qualidade de vida e criação de mods para o DiRT Rally 2.0.**

---

## 🛡️ Política Anti-Cheat & Fair Play (Aviso Importante)

> [!CAUTION]
> **ESTE PROJETO NÃO APOIA, NÃO INCENTIVA E CONDENA O USO DE TRAPAÇAS (CHEATS).**  
> O objetivo do DR2 ModLoader é puramente educacional, voltado ao treino pessoal de pilotos virtuais, pesquisa de telemetria e expansão criativa da comunidade offline de simulação de rally.

### Proteções Integradas (Safety Guards & Network Air-Gap)
Para evitar categoricamente que o mod loader interfira em tabelas de pontuação competitiva, o DR2 ModLoader implementa travas duplas de segurança:

1. **Isolamento de Rede Mandatório (Winsock Air-Gap):**
   - O subsistema `NetworkGuard` intercepta dinamicamente funções do Windows Sockets (`ws2_32.dll`: `getaddrinfo`, `connect`, `sendto`).
   - Tentativas de conexão com servidores de autenticação e ranking da Codemasters, EA e RaceNet são abortadas no nível do sistema operacional.
   - O jogo opera de forma estritamente offline enquanto a DLL proxy estiver presente. Não há como desativar esta proteção em tempo de execução sem remover a DLL.
2. **Bloqueio Hard-Lock em C++ (`SafetyGuard`):**
   - As APIs de modificação de memória (`Player.setPosition`, `Player.setVelocity`, `Player.setState`) realizam validações no núcleo C++ nativo com princípio *fail-closed*.
   - Se o modo de jogo não for um ambiente offline homologado, qualquer escrita é sumariamente rejeitada.
3. **Ambientes Homologados para Treino:**
   - **DirtFish (Área livre de testes)**.
   - **Time Trial Local / Offline**.
   - **Campeonatos Customizados Offline**.

---

## 🎯 Mod de Treino (Practice Mode / Savestate)

Nas etapas longas de rally (que chegam a 15–20 km de extensão), um piloto muitas vezes quer treinar uma curva fechada ou um salto técnico que fica a 10 minutos do início da especial. Errar essa curva exigiria reiniciar a especial inteira e refazer todo o percurso.

O **Practice Mode** oferece duas modalidades de restauração:
- `F5`: **Salva o Checkpoint** (coordenadas espaciais XYZ, orientação de rotação, velocidades lineares e angulares).
- `F6`: **Restaura Normalmente (Normal)** — teleporta o carro com precisão milimétrica para a posição/orientação salva, zerando as velocidades residuais e acomodando a suspensão para um início estável.
- `F7` (ou via menu ImGui): **Restaura com Momentum (With Momentum)** — além da posição e orientação, restaura a **velocidade linear vetorial e a velocidade angular instantânea** em que o checkpoint foi gravado, permitindo repetir curvas de alta velocidade mantendo a dinâmica inercial original do veículo.

---

## 🖥️ Interface In-Game (Dear ImGui Overlay)

Pressione **`Insert`** a qualquer momento durante a corrida para abrir a interface gráfica do **DR2 ModLoader v0.1.0** (totalmente em inglês):

1. **Aba Diagnostics:**
   - **Engine & Powertrain:** RPM real do virabrequim (`PhysicsRig + 0x370`), marcha ativa da transmissão (`+0x390`), velocidade em km/h, torque instantâneo (Nm) e posição do acelerador (0.0 a 1.0).
   - **Vehicle Kinematics:** Posição espacial tridimensional $(X, Y, Z)$, vetor de velocidade linear $(V_x, V_y, V_z)$ e vetor de velocidade angular $(\omega_x, \omega_y, \omega_z)$.
   - **Suspension:** Compressão e contato dos 4 pneus (Front-Left, Front-Right, Rear-Left, Rear-Right).
   - **Session Status:** Ponteiros do jogador, container e physics rig confirmados em tempo real.
2. **Aba Mods:**
   - Visualização dos mods carregados a partir da pasta `/mods`.
   - Status de execução, versões e recarregamento a quente (*hot-reload*).
3. **Aba Practice Mode:**
   - Painel interativo de controle de checkpoints.
   - Botões dedicados: *Save Checkpoint*, *Restore Checkpoint*.
   - Seletor de modo de restauração: **Normal** vs. **With Momentum**.
   - Notificações de confirmação em tela.

---

## ⚙️ Arquitetura do Sistema

```
                        ┌───────────────────────────────┐
                        │   DiRT Rally 2.0 Process      │
                        │      (dirtrally2.exe)         │
                        └──────────────┬────────────────┘
                                       │ Carrega na inicialização
                                       ▼
                        ┌───────────────────────────────┐
                        │       Proxy DLL (dxgi.dll)    │
                        └──────────────┬────────────────┘
                                       │ Inicializa Thread Core
                                       ▼
   ┌────────────────────────────────────────────────────────────────────────┐
   │                        DR2 ModLoader Core                              │
   │                                                                        │
   │  ┌───────────────────────┐             ┌────────────────────────────┐  │
   │  │   MinHook / Detours   │             │   Safety Guard & Gate      │  │
   │  │  (DirectX 11 Present) │             │ (Fail-Closed Memory Check) │  │
   │  └──────────┬────────────┘             └─────────────┬──────────────┘  │
   │             │ Loop a cada frame                      │ Interrompe mods │
   │             ▼                                        ▼ invasivos online│
   │  ┌───────────────────────┐             ┌────────────────────────────┐  │
   │  │   Dear ImGui Overlay  │             │   NetworkGuard             │  │
   │  │ (Diagnostics/Mods/UI) │             │  (Winsock Air-Gap Detours) │  │
   │  └──────────┬────────────┘             └────────────────────────────┘  │
   │             ▼                                                          │
   │  ┌──────────────────────────────────────────────────────────────────┐  │
   │  │                     API & Scripting Engine (Lua 5.4)             │  │
   │  └──────────────────────────────────┬───────────────────────────────┘  │
   └─────────────────────────────────────┼──────────────────────────────────┘
                                         │
                 ┌───────────────────────┴───────────────────────┐
                 ▼                                               ▼
     ┌────────────────────────┐                     ┌────────────────────────┐
     │  mods/practice_mode/   │                     │  mods/outros_mods/     │
     │  (Normal vs Momentum)  │                     │  (Telemetria / Custom) │
     └────────────────────────┘                     └────────────────────────┘
```

---

## 🔌 API de Eventos e Controle

O DR2 ModLoader permite que a comunidade crie mods usando **Lua 5.4** de forma simples e intuitiva:

### Eventos do Jogo
```lua
function onInit()
    print("[MeuMod] Inicializado com sucesso!")
end

function onStageStart(stage)
    print("Iniciando especial: " .. stage.name)
end

function onTick(dt)
    -- Executado a cada frame renderizado (Present hook)
end

function onKeyDown(keyCode)
    -- Captura instantânea de teclas via WndProc hook (ex: 0x74 = F5)
end
```

### Manipulação do Estado e Telemetria
```lua
-- Leitura completa de telemetria nativa do motor e dinâmica
local telem = Player.getVehicleTelemetry()
-- telem.rpm           -> RPM real do virabrequim (PhysicsRig + 0x370)
-- telem.gear          -> Marcha ativa (-1 = R, 0 = N, 1..n)
-- telem.speedKmh      -> Velocidade real em km/h
-- telem.torque        -> Torque instantâneo (Nm)
-- telem.throttle      -> Posição do pedal de acelerador (0.0 a 1.0)

-- Captura o estado completo de física do veículo
local savedState = Player.getState()

-- Restauração Normal (estacionário / sem inércia)
Player.setState(savedState, "normal")

-- Restauração com Momentum (preserva vetor de velocidade linear e velocidade angular)
Player.setState(savedState, "momentum")
```

---

## 📁 Estrutura de Diretórios

```
DR2ModLoader/
├── .gitignore
├── CMakeLists.txt
├── README.md
├── SSOT.md
├── docs/
│   ├── adr_savestate_safety.md  # Registros de decisões de arquitetura (ADRs 001-006)
│   ├── exports_dxgi.md          # Especificação técnica do proxy DXGI
│   ├── INSTALL.md               # Guia de instalação (Windows e Linux/Steam Deck)
│   ├── MODDING_GUIDE.md         # Documentação completa da API Lua para modders
│   └── REVERSE_ENGINEERING.md   # Mapeamento do Physics Rig, offsets e Winsock
├── include/
│   └── dr2hook/
│       ├── api.h                # Exportações C++ públicas
│       ├── hooks.h              # Hooks MinHook (DX11 Present e WndProc)
│       ├── memory.h             # Resolução de ponteiros e pattern scanning
│       ├── network_guard.h      # Winsock detours e isolamento de rede
│       ├── overlay.h            # Interface in-game Dear ImGui
│       ├── player.h             # Controle e telemetria do veículo
│       └── safety.h             # Safety Gate com princípio fail-closed
├── src/
│   ├── core/
│   │   ├── main.cpp             # Ponto de entrada (DllMain)
│   │   ├── hooks.cpp            # Implementação dos hooks gráficos e de janela
│   │   ├── memory.cpp           # Cadeia de ponteiros e inspeção de memória
│   │   ├── network_guard.cpp    # Detours de ws2_32.dll (getaddrinfo, connect, sendto)
│   │   ├── player.cpp           # Leitura de RPM real (0x370), marcha (0x390) e restauração
│   │   └── safety.cpp           # Validação de ambiente seguro
│   ├── proxy/
│   │   └── dxgi_proxy.cpp       # Stubs C++ com repasse dinâmico para System32 dxgi.dll
│   ├── script/
│   │   ├── lua_engine.cpp       # Interpretador Lua 5.4 e bindings C++
│   │   └── mod_manager.cpp      # Gerenciador de módulos da pasta mods/
│   └── ui/
│       └── overlay.cpp          # Renderização Dear ImGui (Diagnostics, Mods, Practice Mode)
└── mods/
    └── practice_mode/
        ├── mod.json             # Manifesto de metadados
        └── main.lua             # Lógica do savestate com Normal e Momentum
```

---

## 🛠️ Como Compilar

### Pré-requisitos
- **CMake** 3.20 ou superior
- **GCC / MinGW-w64** (Linux cross-compile) ou **Visual Studio 2022** (MSVC x64 C++20)

### Compilação Cruzada no Linux (Recomendado)
```bash
# Executa a suíte de testes unitários e constrói a DLL Windows de release
bash build_release.sh
```

### Compilação no Windows (MSVC)
```bash
mkdir build && cd build
cmake .. -A x64
cmake --build . --config Release
```

O binário resultante (`dxgi.dll`) e a pasta `mods/` devem ser colocados na pasta raiz do jogo:
`[SteamLibrary]/steamapps/common/DiRT Rally 2.0/`

---

## 🗺️ Roadmap de Desenvolvimento

- [x] **Fase 0:** Engenharia reversa da EGO Engine, mapeamento do Physics Rig, RPM real (`0x370`) e marcha (`0x390`)
- [x] **Fase 1:** Fundação da Proxy DLL (`dxgi.dll`) com repasse dinâmico para `System32` sem dependência de `.def`
- [x] **Fase 2:** Hooks de `IDXGISwapChain::Present` e `WndProc` via MinHook com renderização assíncrona
- [x] **Fase 3:** Safety Gate com princípio *fail-closed* e isolamento de rede obrigatório (`NetworkGuard`)
- [x] **Fase 4:** Motor de scripting Lua 5.4 com sandboxing e bindings completos da API `Player`, `Safety` e `UI`
- [x] **Fase 5:** Interface in-game Dear ImGui com abas Diagnostics, Mods e Practice Mode (em inglês)
- [x] **Fase 6:** Suíte de 1.765 testes automatizados, verificação de release e documentação completa

---

## 📄 Licença
Este projeto é distribuído sob a licença **MIT**. Consulte o arquivo `LICENSE` para mais detalhes. O projeto não possui qualquer vínculo oficial com a Codemasters ou Electronic Arts.
