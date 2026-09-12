# DR2Hook — Mod Loader & Hook API para DiRT Rally 2.0

> **Framework de modding extensível e voltado para treino, pesquisa, qualidade de vida e criação de mods para o DiRT Rally 2.0.**

---

## 🛡️ Política Anti-Cheat & Fair Play (Aviso Importante)

> [!CAUTION]
> **ESTE PROJETO NÃO APOIA, NÃO INCENTIVA E CONDENA O USO DE TRAPAÇAS (CHEATS).**
> O objetivo do DR2Hook é puramente educacional, voltado ao treino pessoal de pilotos virtuais, pesquisa de telemetria e expansão criativa da comunidade offline de simulação de rally.

### Proteções Integradas (Safety Guards & Killswitch)
Para evitar que qualquer usuário ative acidentalmente funcionalidades de teleporte ou modificação de estado em sessões competitivas, o DR2Hook implementará:

1. **Detecção de Sessão e Modo de Jogo:**
   - O core do loader monitora o estado de conexão e os modos de jogo ativos.
2. **Bloqueio Automático em Eventos Oficiais:**
   - **Desafios Diários, Semanais e Mensais do RaceNet.**
   - **Clubes com Placares Oficiais e Campeonatos Ranqueados.**
   - Sempre que o jogo entrar em qualquer evento sujeito a ranqueamento mundial, as APIs de modificação de memória (como `Player.setPosition`, `Player.setVelocity`, etc.) são **automaticamente desativadas (hard-lock)**.
3. **Ambientes Homologados para Treino:**
   - **DirtFish (Área livre de testes)**.
   - **Time Trial Local / Offline**.
   - **Campeonatos Customizados Offline**.

---

## 🎯 Objetivo Inicial: Mod de Treino (Practice Mode / Savestate)

Nas etapas longas de rally (que chegam a 15–20 km de extensão), um piloto muitas vezes quer treinar uma curva fechada ou um salto técnico que fica a 10 minutos do início da especial. Hoje, errar essa curva exige reiniciar a especial inteira e refazer todo o percurso.

O **Practice Mode** resolverá isso:
- `F5` / Botão configurável no volante: **Salva o Checkpoint** (posição espacial, rotação, velocidade vetorial e inércia).
- `F6` / Botão configurável: **Restaura o Checkpoint** instantaneamente, permitindo repetir a curva ou setor sem telas de carregamento e sem reiniciar a corrida.

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
   │                            DR2Hook Core                                │
   │                                                                        │
   │  ┌───────────────────────┐             ┌────────────────────────────┐  │
   │  │   MinHook / Detours   │             │   Safety Guard / Gate      │  │
   │  │  (DirectX 11 Present) │             │ (RaceNet/Online Detection) │  │
   │  └──────────┬────────────┘             └─────────────┬──────────────┘  │
   │             │ Loop a cada frame                      │ Interrompe mods │
   │             ▼                                        ▼ invasivos online│
   │  ┌──────────────────────────────────────────────────────────────────┐  │
   │  │                     API & Scripting Engine (Lua)                 │  │
   │  └──────────────────────────────────┬───────────────────────────────┘  │
   └─────────────────────────────────────┼──────────────────────────────────┘
                                         │
                 ┌───────────────────────┴───────────────────────┐
                 ▼                                               ▼
     ┌────────────────────────┐                     ┌────────────────────────┐
     │  practice_mode.lua     │                     │  custom_hud.lua        │
     │  (Savestate / Teleport)│                     │  (Telemetria / UI)     │
     └────────────────────────┘                     └────────────────────────┘
```

---

## 🔌 API de Eventos e Controle (Planejada)

O DR2Hook foi desenhado para que a comunidade possa criar mods usando **Lua** ou **C++** de forma simples e intuitiva:

### Eventos do Jogo
```lua
function onGameInit()
    print("[DR2Hook] Jogo inicializado com sucesso!")
end

function onStageStart(stageData)
    print("Iniciando especial: " .. stageData.name)
end

function onStagePause(isPaused)
    -- Disparado ao pausar/despausar a corrida
end

function onTick(deltaTime)
    -- Executado a cada frame renderizado
end
```

### Manipulação do Estado do Jogador
```lua
-- Obter e definir propriedades do carro (com restrições de segurança ativas)
local pos = Player.getPosition()     -- { x = ..., y = ..., z = ... }
local vel = Player.getVelocity()     -- { vx = ..., vy = ..., vz = ... }
local rot = Player.getRotation()     -- Matriz / Quaternion

-- Exemplo: Restaurar estado anterior
Player.setState(savedState)
```

---

## 📁 Estrutura de Diretórios Proposta

```
dr2-modloader/
├── .gitignore
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── memory_layout.md         # Anotações de engenharia reversa e offsets
│   └── safety_guidelines.md     # Detalhamento das regras anti-cheat
├── include/
│   └── dr2hook/
│       ├── api.h                # Cabeçalho C++ público para modders
│       ├── events.h             # Definição de eventos do ciclo de vida
│       └── player.h             # Interface de controle do veículo
├── src/
│   ├── core/
│   │   ├── main.cpp             # Ponto de entrada (DllMain)
│   │   ├── hooks.cpp            # Implementação dos hooks (MinHook / DX11)
│   │   ├── memory.cpp           # Escaneamento de padrões (AOB Pattern Scanner)
│   │   └── safety.cpp           # Verificações de segurança e bloqueio online
│   ├── proxy/
│   │   └── dxgi_proxy.cpp       # Redirecionamento das funções originais do Windows
│   └── script/
│       └── lua_engine.cpp       # Interpretador Lua / Sol2
└── mods/
    └── practice_mode/
        ├── mod.json             # Metadados do mod
        └── main.lua             # Lógica do savestate / checkpoint
```

---

## 🛠️ Como Compilar

### Pré-requisitos
- **CMake** 3.20 ou superior
- **Visual Studio 2022** (MSVC x64) ou Clang com suporte a C++20
- **Windows 10/11** x64

### Passos de Build
```bash
# Clone ou navegue até o repositório
cd dr2-modloader

# Crie a pasta de compilação
mkdir build && cd build

# Gere o projeto para 64 bits (o executável do DR2 é x64)
cmake .. -A x64

# Compile em modo Release
cmake --build . --config Release
```

O binário resultante (`dxgi.dll`) e a pasta `mods/` devem ser colocados na pasta raiz de instalação do jogo:
`.../steamapps/common/DiRT Rally 2.0/`

---

## 🗺️ Roadmap de Desenvolvimento

- [x] Definição de arquitetura e política ética anti-cheat
- [ ] Mapeamento inicial de memória do veículo (coordenadas, rotação, inércia) via DirtFish
- [ ] Criação do wrapper DLL Proxy (`dxgi.dll`) com MinHook e DX11 Present Hook
- [ ] Implementação das verificações de segurança (`safety.cpp`) para bloqueio em modos competitivos
- [ ] Protótipo funcional do Savestate / Restauração em C++
- [ ] Integração do interpretador Lua e exposição dos primeiros hooks para a pasta `/mods`
- [ ] Interface visual in-game (ImGui) com notificações visuais

---

## 📄 Licença
Este projeto é distribuído sob a licença **MIT**. Consulte o arquivo `LICENSE` para mais detalhes. O projeto não possui qualquer vínculo oficial com a Codemasters ou Electronic Arts.
