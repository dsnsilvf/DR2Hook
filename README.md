# DR2Hook

O **DR2Hook** é um projeto de engenharia reversa e instrumentação para **DiRT Rally 2.0** (EGO Engine, x64, DirectX 11). Ele mapeia a física do veículo, limites de tick e dados de UI com notas classificadas por evidência, hooks opcionais no tick de física e ferramentas de captura offline. Sobre esse trabalho vem o **mod loader**: mods em Lua, overlay in-game, checkpoints de treino e integração nativa ao menu de pausa, com a marca **DR2 ModLoader v0.1.0** na interface.

Este projeto não tem vínculo com Codemasters ou Electronic Arts.

## Engenharia reversa (foco principal)

Os achados estão divididos por subsistema e mantêm os rótulos de confiança originais (`CONFIRMED`, `PROBABLE`, `HYPOTHESIS`, `REFUTED` e variantes em português quando a fonte usou).

| Recurso | O que é |
| --- | --- |
| [docs/reverse_engineering/README.md](docs/reverse_engineering/README.md) | Índice: cadeia de ponteiros do PhysicsRig, rodas, suspensão, motor, câmbio, telemetria UDP, dados de UI e tópicos relacionados |
| [docs/reverse_engineering/investigations/INV-01/HANDOFF.md](docs/reverse_engineering/investigations/INV-01/HANDOFF.md) | **INV-01** — investigação do estado do carro: bloco de origem no rig, ordem do tick, candidatos à API nativa de estado, status do harness, gates de validação |
| [docs/reverse_engineering/investigations/INV-01/kb/](docs/reverse_engineering/investigations/INV-01/kb/) | Base de conhecimento graduada (`confirmed-facts`, `hypotheses`, `open-questions`, `history`, `glossary`) |
| [docs/physics_tick_harness.md](docs/physics_tick_harness.md) | Hooks opt-in no caminho do tick de física da EGO, log CSV, escritas enfileiradas, chamadas nativas experimentais |
| [docs/BLACKBOX.md](docs/BLACKBOX.md) | **`scripts/dr2rec`** — gravador e analisador de sessão offline (somente leitura; sem escrita no jogo, sem RaceNet) |

As leituras em runtime usadas pelo overlay e pelas APIs `Player` estão em `src/core/player.cpp`, nos offsets documentados do rig. Depois de alterar só a lógica nativa, recompile `dr2hook_core.dll`, substitua o arquivo ao lado de `dirtrally2.exe` e pressione **F8** — a proxy DXGI e os hooks de rede permanecem carregados.

## Mod loader (construído sobre o trabalho de RE)

Pressione **Insert** no jogo para abrir o overlay Dear ImGui (interface em inglês). Também é possível abrir pelo item **DR2 ModLoader** no menu de pausa quando os hooks correspondem ao build esperado do jogo (veja [docs/reverse_engineering/menu.md](docs/reverse_engineering/menu.md)).

### Checkpoints de treino

Especiais longas tornam repetir uma curva penoso. Os checkpoints guardam pose e velocidade e restauram sem reiniciar a especial.

| Tecla | Ação |
| --- | --- |
| **F5** | Salvar checkpoint (posição, orientação, velocidade linear e angular). |
| **F6** | Restaurar com o modo selecionado no overlay (**Normal** ou **With Momentum**). |
| **F7** | Restaurar **With Momentum** (velocidades linear e angular do momento salvo). |
| **F8** | Recarregar `dr2hook_core.dll` do disco. O jogo continua aberto; o checkpoint C++ em memória é limpo e o Lua reinicia (`onInit` roda de novo). |

A aba **Practice Mode** e o mod incluído `mods/practice_mode/` expõem os mesmos atalhos. A aba do overlay usa o `SavestateManager` em C++. O mod mantém checkpoint próprio em Lua via `Player.getState` / `Player.setState` e adiciona opções em **Pause → DR2 Hook → Mods** (API `Menu`).

A restauração **Normal** grava a pose no rig e zera velocidade linear/angular nos offsets documentados (`+0x320` / `+0x330`). **With Momentum** preserva as velocidades salvas. Isso não é o pipeline nativo completo de “reset vehicle”; a estabilidade depende do solver de física.

### Abas do overlay

- **Diagnostics** — RPM (virabrequim em rad/s em `+0x13d8`), marcha (`+0x1448`), corte, velocidade, posição, velocidades linear e angular, compressão de suspensão e contato com o solo por roda, e se os ponteiros player / container / physics-rig resolvem.
- **Mods** — mods Lua em `mods/`. **Reload Scripts** reinicia só o Lua. **Reload Native Core (F8)** recarrega `dr2hook_core.dll`.
- **Practice Mode** — salvar, restaurar e escolher Normal vs With Momentum (checkpoint C++).

## Instalação

Compile ou baixe `dxgi.dll` e `dr2hook_core.dll` (veja [Build](#build)), depois copie os dois e a pasta `mods/` para o diretório do jogo:

`[SteamLibrary]/steamapps/common/DiRT Rally 2.0/`

O jogo carrega `dxgi.dll` na inicialização. A proxy encaminha para o `dxgi.dll` real em `System32` e carrega `dr2hook_core.dll` na mesma pasta. Não é necessário injetor externo.

Passo a passo para Windows e Linux / Steam Deck: [docs/INSTALL.md](docs/INSTALL.md).

## Physics tick harness (opt-in)

Somente engenharia reversa e instrumentação. Hooks no caminho do tick de física da EGO, log CSV de amostras do rig, **escritas na memória** enfileiradas opcionais e chamadas opcionais à **API nativa experimental** (`SetTransform`, `SetLinVel`, `SetAngVel`, `Commit` nos VAs da spec). Tudo **desligado por padrão**.

| Habilitar | Env / INI |
| --- | --- |
| Instrumentação + CSV | `DR2HOOK_PHYSICS_HARNESS=1` ou `instrumentation=1` |
| Escritas enfileiradas no rig | `DR2HOOK_PHYSICS_HARNESS_WRITES=1` ou `writes=1` |
| Chamadas nativas experimentais | `DR2HOOK_PHYSICS_HARNESS_EXPERIMENTAL_NATIVE=1` ou `experimental_native=1` |
| Self-test (sem escritas reais) | `DR2HOOK_PHYSICS_HARNESS_SELF_TEST=1` ou `self_test=1` |

Chamadas nativas exigem instrumentação, a flag experimental e `SafetyGuard::CanWriteState()`. Rodam só em fronteiras de física escolhidas (nunca em `Present`). Detalhes: [docs/physics_tick_harness.md](docs/physics_tick_harness.md).

## Build

Requisitos: CMake 3.20+ e MinGW-w64 (cross-compile no Linux) ou Visual Studio 2022 (MSVC, x64, C++20).

Linux, verificação completa (testes unitários e pacote de release):

```bash
bash scripts/verify_release.sh
```

Linux, só as DLLs de release:

```bash
bash scripts/package_release.sh
```

O pacote contém `dxgi.dll`, `dr2hook_core.dll` e `mods/`. Os dois binários são linkados estáticamente (sem `libstdc++` ou `libgcc` na pasta do jogo).

Windows:

```bash
mkdir build && cd build
cmake .. -A x64
cmake --build . --config Release --target dxgi --target dr2hook_core
```

## Caixa-preta do veículo (`dr2rec`)

`scripts/dr2rec` grava uma sessão local e analisa depois. A captura guarda bytes brutos; nomes de campo limitam-se ao que já está confirmado. Não escreve na memória do jogo nem contata a RaceNet. Manual: [docs/BLACKBOX.md](docs/BLACKBOX.md).

## Escrevendo um mod

Mods são scripts Lua 5.4 em `mods/<name>/` com manifesto `mod.json`. O exemplo incluído é `mods/practice_mode/`.

Callbacks de ciclo de vida suportados hoje: `onInit`, `onTick(dt)` e `onKeyDown(keyCode)` (veja [docs/MODDING_GUIDE.md](docs/MODDING_GUIDE.md)). `onStageStart` está registrado no loader, mas **ainda não é disparado pelo gameplay** (só testes unitários chamam).

Bindings Lua: `Player` (`getPosition`, `setPosition`, `getVelocity`, `setVelocity`, `getState`, `setState`), `Safety.isRestrictedMode()`, `UI.notify` e `Menu` (submenu nativo na pausa). **Não** existe `Player.getVehicleTelemetry()` no Lua; RPM e marcha aparecem no overlay **Diagnostics** via código nativo.

```lua
function onInit()
    print("[MyMod] ready")
end

function onTick(dt)
    -- once per rendered frame (Present)
end

function onKeyDown(keyCode)
    if keyCode == 0x74 then -- F5
        local saved = Player.getState()
        Player.setState(saved, "normal")
    end
end
```

`Player.setPosition`, `Player.setVelocity` e `Player.setState` chamam `SafetyGuard::CanWriteState()` antes de escrever. O guard pode restringir escritas por modo de sessão quando o endereço de sessão está configurado e o modo permissivo está desligado; **o v0.1.0 inicia o core com modo permissivo habilitado**, então as restrições não valem in-game até essa integração terminar.

Referência completa da API: [docs/MODDING_GUIDE.md](docs/MODDING_GUIDE.md). Mapas de ponteiros e offsets: [docs/reverse_engineering/README.md](docs/reverse_engineering/README.md).

## Como o projeto se organiza

```
dirtrally2.exe
    └── dxgi.dll          stays loaded: DXGI proxy, Present, WndProc, NetworkGuard
            └── dr2hook_core.dll    overlay, telemetry, Lua, savestate (F8 reloads this file)
```

| Caminho | Função |
| --- | --- |
| `src/proxy/dxgi_proxy.cpp` | Proxy DXGI |
| `src/core/hooks.cpp` | `Present` DirectX 11, window procedure e hooks Winsock (residentes na proxy) |
| `src/core/host.cpp` | Carrega `dr2hook_core.dll` e recarrega com F8 |
| `src/core/core_module.cpp` | Entrada recarregável: frame, input, init, shutdown |
| `src/core/safety.cpp` | Portão de escrita por sessão (fail-closed quando configurado) |
| `src/core/player.cpp` | Leituras de telemetria e restauração de checkpoint |
| `src/core/physics_tick_harness.cpp` | Instrumentação de física opt-in |
| `src/script/` | Runtime Lua e carregamento de mods |
| `src/ui/overlay.cpp` | Overlay ImGui |
| `docs/` | Instalação, modding, engenharia reversa, harness, notas de arquitetura |

**F8** (ou **Reload Native Core** na aba Mods) encerra o core, descarrega e carrega uma cópia nova. Substitua `dr2hook_core.dll` ao lado de `dirtrally2.exe` e pressione **F8**. O host copia o arquivo para `dr2hook_core.N.dll` antes de carregar, para o build poder sobrescrever `dr2hook_core.dll` com o jogo aberto. O checkpoint C++ é limpo e o Lua recomeça. **F8** é tratado no caminho da proxy e não chega aos mods.

Mudanças em `dxgi.dll` ainda exigem reiniciar o jogo. A proxy, MinHook, `Present`, `WndProc` e `NetworkGuard` permanecem mapeados.

## Status

- Documentação de RE graduada por evidência, árvore da investigação INV-01 e ferramentas de captura `dr2rec`
- Physics tick harness opt-in (desligado por padrão)
- DLL proxy com encaminhamento dinâmico para `System32`
- `dr2hook_core.dll` recarregável (**F8**) para overlay, telemetria, savestate e Lua
- Hooks em `Present` e `WndProc`, overlay ImGui
- `NetworkGuard` com bloqueio mandatório de rede enquanto a proxy estiver carregada
- Sandbox Lua 5.4 com bindings `Player`, `Safety`, `UI` e `Menu`
- Checkpoints de treino (Normal / With Momentum) via savestate C++ e mod Lua incluído
- Testes automatizados, scripts de release e documentação

## Fair play

O DR2Hook é voltado a treino offline, pesquisa de telemetria e mods da comunidade — não a placares ou competição RaceNet.

**Rede.** Enquanto `dxgi.dll` estiver carregada, o `NetworkGuard` intercepta `ws2_32.dll` e recusa tráfego remoto de forma explícita:

- `getaddrinfo` / `GetAddrInfoW` para qualquer nome que não seja localhost retorna “name not found” (registrado em log).
- `connect` fora de `127.0.0.0/8` e do loopback IPv6 retorna `WSAECONNREFUSED` (10061).
- Localhost permanece liberado para ferramentas locais (SimHub, motion rigs, telemetria em `127.0.0.1`).

Não há menu nem API de script para desligar isso. Jogo online só volta depois de sair e remover `dxgi.dll` e `dr2hook_core.dll`.

**Escritas na memória.** O `SafetyGuard` foi pensado para permitir restauração de posição, velocidade e estado completo só em DirtFish, time trial offline e campeonatos custom offline quando a detecção de sessão está ativa e o modo permissivo está desligado. Modo desconhecido, leitura falha ou ponteiro ausente é tratado como não permitido. Veja a nota do mod loader acima sobre o comportamento atual do v0.1.0.

## Licença

MIT. Veja `LICENSE`.
