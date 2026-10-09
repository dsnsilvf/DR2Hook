# Guia Oficial da API de Mods do DR2Hook

> Documenta a API da v0.1.0 e o que foi acrescentado depois dela (`onStageLoad`, `onCountdown`, `onStageStart`, `Race`, `Ghost`, `Menu.set` e `Menu.describe`). O pacote de release ainda se chama v0.1.0.

Bem-vindo ao Guia Oficial de Desenvolvimento de Mods para o **DR2Hook v0.1.0** no *DiRT Rally 2.0*.

O DR2Hook oferece um ambiente de scripting modular e sandbox baseado em **Lua 5.4**, permitindo que a comunidade crie ferramentas de telemetria, modos de treino, interfaces personalizadas e automações em tempo real com overhead imperceptível de processamento (< 0.2ms por frame).

---

## 1. Estrutura de um Mod

Todos os mods devem residir em subpastas dedicadas dentro do diretório `mods/` na raiz do jogo:

```
[Pasta do Jogo]/
└── mods/
    └── meu_mod_exemplo/
        ├── mod.json       (Obrigatório: Manifesto de metadados do mod)
        └── main.lua       (Obrigatório: Ponto de entrada do script Lua)
```

### Especificação do Manifesto (`mod.json`)

O arquivo `mod.json` descreve as propriedades do mod e define o script a ser executado pelo carregador:

```json
{
  "name": "Meu Mod de Exemplo",
  "id": "comunidade.meu_mod",
  "version": "1.0.0",
  "author": "Seu Nome ou Equipe",
  "description": "Descrição detalhada dos recursos oferecidos pelo mod.",
  "main": "main.lua"
}
```

| Campo | Tipo | Obrigatório | Descrição |
| :--- | :--- | :---: | :--- |
| `name` | String | Não (usa `id`) | Nome amigável exibido no menu in-game ImGui (`Insert`). |
| `id` | String | Não (usa pasta)| Identificador único do mod no ecossistema (ex: `dr2.practice_mode`). |
| `version` | String | Não (padrão `1.0.0`) | Versão semântica do mod (`MAJOR.MINOR.PATCH`). |
| `author` | String | Não | Autor ou equipe responsável. |
| `description` | String | Não | Breve resumo da utilidade do mod. |
| `main` | String | Não (padrão `main.lua`) | Nome do arquivo Lua principal dentro da pasta do mod. |

---

## 2. Callbacks de Ciclo de Vida

O DR2Hook invoca funções globais específicas no seu script `main.lua` caso estejam declaradas:

### `onInit()`
- **Quando é chamado:** Uma vez quando o `ModManager` carrega o mod: na abertura do jogo, no botão **Reload Scripts (Hot-Reload)** e também depois de **F8**, porque o reload nativo sobe o Lua de novo.
- **Assinatura:** `function onInit()`
- **Exemplo:**
  ```lua
  function onInit()
      print("[MeuMod] Inicializado com sucesso!")
  end
  ```

### `onTick(dt)`
- **Quando é chamado:** Executado a cada frame de renderização do jogo, no hook do DirectX 11 `Present`.
- **Parâmetros:**
  - `dt` (number): Tempo decorrido desde o frame anterior em segundos (ex: `0.0166` para ~60 FPS).
- **Assinatura:** `function onTick(dt)`
- **Exemplo:**
  ```lua
  function onTick(dt)
      -- Telemetria por frame ou checagem contínua
  end
  ```

### `onKeyDown(keyCode)`
- **Quando é chamado:** Disparado instantaneamente pela `WndProc` do jogo quando uma tecla é pressionada (`WM_KEYDOWN`).
- **Parâmetros:**
  - `keyCode` (number): Código de tecla virtual do Windows (Virtual Key Code).
- **Atalhos comuns:**
  - `0x70` a `0x7B`: Teclas de função `F1` a `F12` (`F5` = `0x74`, `F6` = `0x75`, `F7` = `0x76`).
  - `0x2D`: Tecla `Insert`, consumida pelo menu. Não chega em `onKeyDown`.
  - `0x77`: Tecla `F8`, consumida pela proxy para recarregar `dr2hook_core.dll`. Não chega em `onKeyDown`.
  - `0x20`: Barra de Espaço.
- **Assinatura:** `function onKeyDown(keyCode)`
- **Exemplo:**
  ```lua
  function onKeyDown(keyCode)
      if keyCode == 0x74 then -- F5
          print("Tecla F5 pressionada!")
      end
  end
  ```

### `onStageLoad(stage)`
- **Quando é chamado:** No início do carregamento de uma especial, quando o jogo abre o pacote da localidade (`locations/<local>__<pista>.nefs`). Reiniciar a especial não recarrega e não chama este callback.
- **Parâmetros:**
  - `stage` (table): campo `name` (string) com a pista, ex.: `"new_zealand_rally_01"`. A rota ainda não é identificada.
- **Assinatura:** `function onStageLoad(stage)`

### `onCountdown(light)`
- **Quando é chamado:** Uma vez por luz da contagem de largada, com um segundo entre elas.
- **Parâmetros:**
  - `light` (integer): número da luz, de `1` a `5`. A largada (`onStageStart`) vem um segundo depois da quinta.
- **Assinatura:** `function onCountdown(light)`

### `onStageStart(stage)`
- **Quando é chamado:** Na largada, quando o jogador assume o controle do carro (evento `racestart` do jogo). Também na largada depois de reiniciar a especial.
- **Parâmetros:**
  - `stage` (table):
    - `name` (string): pista, igual a `onStageLoad`.
    - `restart` (boolean): `true` quando é a largada de um reinício da mesma especial, sem novo carregamento.
- **Assinatura:** `function onStageStart(stage)`
- **Exemplo:**
  ```lua
  function onStageStart(stage)
      if stage.restart then
          print("[MeuMod] Especial reiniciada: " .. stage.name)
      else
          print("[MeuMod] Nova especial: " .. stage.name)
      end
  end
  ```

---

## 3. Tabela de APIs Nativas Expostas

O DR2Hook expõe namespaces protegidos em C++ para o ambiente Lua:

### Módulo `Player`
Controla leitura e escrita de cinemática, telemetria do motor e suspensão do veículo do jogador.

| Função | Parâmetros | Retorno | Descrição |
| :--- | :--- | :--- | :--- |
| `Player.getPosition()` | Nenhum | `table` ou `nil` | Retorna `{ x = number, y = number, z = number }`. |
| `Player.setPosition(pos)` | `pos` (table: `{x, y, z}`) | `boolean` | Altera a posição tridimensional do veículo na pista. |
| `Player.getVelocity()` | Nenhum | `table` ou `nil` | Retorna velocidades nos eixos cartesianos `{ x, y, z, vx, vy, vz }`. |
| `Player.setVelocity(vel)` | `vel` (table: `{vx, vy, vz}`) | `boolean` | Altera a velocidade linear do veículo. |
| `Player.getState()` | Nenhum | `table` ou `nil` | Captura o estado completo de física do veículo (cinemática, orientação, velocidades e suspensões). |
| `Player.setState(state, mode)` | `state` (table), `mode` (string opcional: `"normal"` ou `"momentum"`) | `boolean` | Restaura estado do veículo. Em modo `"normal"` (padrão), zera as velocidades e estabiliza o carro. Em modo `"momentum"`, preserva velocidades lineares e angulares gravadas. |
| `Player.getVehicleTelemetry()` | Nenhum | `table` ou `nil` | Retorna telemetria avançada: `{ rpm, gear, speedKmh, torque, throttle, position, linearVelocity, angularVelocity }`. |

#### Estrutura Completa de `Player.getState()` / `Player.setState()`
```lua
{
    position = { x = 0.0, y = 0.0, z = 0.0 },
    linearVelocity = { x = 0.0, y = 0.0, z = 0.0, vx = 0.0, vy = 0.0, vz = 0.0 },
    angularVelocity = { x = 0.0, y = 0.0, z = 0.0 },
    rotation = {
        { 1.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 1.0 }
    },
    wheels = {
        { suspensionCompression = 0.35, angularVelocity = 0.0, inContact = true },
        { suspensionCompression = 0.35, angularVelocity = 0.0, inContact = true },
        { suspensionCompression = 0.35, angularVelocity = 0.0, inContact = true },
        { suspensionCompression = 0.35, angularVelocity = 0.0, inContact = true }
    }
}
```

### Módulo `Safety`
Permite inspecionar o status das travas de Fair Play antes de realizar qualquer alteração de estado.

| Função | Parâmetros | Retorno | Descrição |
| :--- | :--- | :--- | :--- |
| `Safety.isRestrictedMode()` | Nenhum | `boolean` | Retorna `true` se o jogo estiver em evento oficial/competitivo (RaceNet), ou `false` se estiver em modo livre/treino offline. |

### Módulo `Race`
- `Race.setStartMode(mode)`: como a largada acontece, inclusive nos reinícios. `mode` é uma string:
  - `"normal"`: padrão do jogo (segurar o freio de mão e 5 luzes).
  - `"no_countdown"`: segura o freio de mão e larga na hora, sem luzes nem a espera do tempo do rival.
  - `"automatic"`: larga sozinho assim que o carro está na linha.
  - `"on_throttle"`: larga ao pisar no acelerador.
  Os hooks ficam no core (recarregados pelo F8, que volta o modo para `"normal"`); retorna `false` fora do Windows.

### Módulo `Ghost`
Carros fantasma (GhostLab). Detalhes de como o jogo guarda e reproduz fantasmas: [ghosts.md](../reverse_engineering/ghosts.md).

| Função | Parâmetros | Retorno | Descrição |
| :--- | :--- | :--- | :--- |
| `Ghost.status()` | Nenhum | `table` | `{ active, slots, readySlots, lapSeconds, ghostSeconds, playerSeconds, delta, gapMeters, offTrackMeters }`. `delta` é a diferença de tempo para o fantasma (positivo = jogador atrás); `gapMeters`, a distância acumulada no trajeto. |
| `Ghost.clone(count, spacing)` | `count` inteiro (`0` desliga), `spacing` em segundos (opcional, padrão `2.0`) | `nil` | Pede `count` cópias da volta do fantasma, espaçadas no tempo. O jogo recria os slots a cada especial: chame de novo em `onStageStart`. |
| `Ghost.setOpaque(enabled)` | `boolean` | `nil` | Fantasma sólido em vez de translúcido. |
| `Ghost.setTimeOffset(seconds)` | `number` | `nil` | Desloca o fantasma no tempo (vantagem ou atraso). |
| `Ghost.setHud(enabled)` | `boolean` | `nil` | Mostra ou esconde a diferença ao vivo no topo da tela. |
| `Ghost.setCars(n)` | inteiro: `0` (ou `1`) = padrão do jogo, `2` a `15` | `integer` | Quantos carros fantasma o jogo cria na **próxima carga da especial pelo menu** (o original e cópias automáticas, espaçadas pelo último `spacing` de `Ghost.clone`). Precisa de um fantasma carregado. Retorna quantos valem agora: se `dr2hook_ghost_cars.txt` existir, ele tem precedência. |

O jogo cria poucos carros fantasma sozinho; `Ghost.clone` só mostra cópias nos carros que já existem. Para mais carros na tela, use `Ghost.setCars` (ou, na pesquisa, o arquivo `dr2hook_ghost_cars.txt`, ver [install.md](install.md)). O máximo estável é **15 fantasmas + o jogador**.

### Módulo `Debug`
Teclas de pesquisa do core.

| Função | Parâmetros | Retorno | Descrição |
| :--- | :--- | :--- | :--- |
| `Debug.setHotkey(name, enabled)` | `name`: `"free_camera"` (F9) ou `"insta_crash"` (F11). `enabled` boolean | `nil` | Liga ou desliga a tecla física. Desligada, ela segue para o jogo; o F9 ainda desliga uma câmera livre já ligada, e o comando remoto `key` continua valendo. Volta a ligada no F8 (o `onInit` dos mods reaplica). Nome desconhecido gera erro de Lua. |

### Módulo `UI`
Permite emitir mensagens e avisos visuais na tela através do sistema de notificações HUD do Dear ImGui.

| Função | Parâmetros | Retorno | Descrição |
| :--- | :--- | :--- | :--- |
| `UI.notify(message, duration)` | `message` (string), `duration` (number opcional, padrão `3.0`) | `nil` | Exibe uma notificação toast semi-transparente no canto superior da tela pelo tempo indicado (em segundos). |

### Módulo `Menu`
Declara opções na tela nativa do jogo: **Pausa > DR2 Hook > aba Mods (LB/RB troca de aba) > nome do mod**. A tela tem o título `OPTIONS` (com o nome do mod em vermelho acima), e o painel da direita acompanha a linha em foco. As chamadas ficam no corpo do `main.lua`, fora dos callbacks. Cada mod tem até 24 opções, numa lista com rolagem, na ordem em que foram declaradas. Declarar de novo o mesmo `id` substitui a opção. Toggle e choice aparecem como `< valor >` e mudam com esquerda e direita. Botões rodam com A. O callback é chamado no quadro seguinte, na thread do `Present`.

| Função | Parâmetros | Retorno | Descrição |
| :--- | :--- | :--- | :--- |
| `Menu.toggle(id, label, [default], [onChange])` | `default` boolean, padrão `false`. `onChange(enabled)` | `nil` | Liga e desliga. Rótulo `label: On` ou `label: Off`. |
| `Menu.choice(id, label, values, [defaultIndex], [onChange])` | `values` lista de strings não vazia. `defaultIndex` começa em `1`. `onChange(index, value)` | `nil` | Avança para o próximo valor e volta ao primeiro depois do último. Rótulo `label: valor`. |
| `Menu.button(id, label, onClick)` | `onClick()` obrigatório | `nil` | Só chama a função. Rótulo `label`. |
| `Menu.get(id)` | `id` | toggle: `boolean`. choice: `index, value`. button ou id inexistente: `nil` | Valor atual. |
| `Menu.set(id, value)` | toggle: `boolean`. choice: índice a partir de `1` | `nil` | Muda o valor sem chamar o callback. Erro se o id não existir ou o índice estiver fora da lista. Com a tela do mod aberta, o combo só mostra o valor novo quando a tela é reaberta. |
| `Menu.describe(id, text)` | `id` de uma opção já declarada. `text` string | `nil` | Texto do painel da direita quando a linha está em foco. Opcional: sem ele, o painel mostra o nome da opção e a descrição do mod (`mod.json`). Erro se o id não existir. |

Uma opção além da 24ª, `values` vazio, `button` sem função e `Menu.*` chamado fora de um mod (por exemplo, pelo console) geram erro de Lua. No carregamento, o mod é desativado como em qualquer outro erro de carga. Mod desativado aparece na lista como `nome (error)`, sem opções. Os valores de toggle e choice são salvos em `mods/<pasta do mod>/settings.ini` (uma linha `id=valor`; toggle `on`/`off`, choice pelo texto do valor) sempre que o jogador muda uma opção no menu ou o mod chama `Menu.set`. Ao carregar o mod, depois do `main.lua` declarar as opções e antes do `onInit`, os valores salvos são aplicados **sem chamar os callbacks**: leia-os com `Menu.get` no `onInit`. Valores que não existem mais na lista e ids desconhecidos são ignorados. Apagar o arquivo volta tudo ao padrão.

Nas descrições (`Menu.describe`), `\n` quebra a linha. Negrito: `*texto*` (ou `**texto**`) sai em DIN negrito no painel; `\\*` mostra um asterisco. Ex.: `Menu.describe("race_start", "*Normal:* hold the handbrake")`. Avançado: um texto que já começa com `{v}` passa direto para a marcação do jogo (`{s:<estilo>}`, ids de `frontend/configs/text_styles.xml`; o painel usa `_22_roboto_cnd`).

```lua
Menu.toggle("free_camera_key", "Free camera key (F9)", true, function(enabled)
    Debug.setHotkey("free_camera", enabled)
end)
Menu.choice("restore_mode", "Restore mode", {"Normal", "Momentum"}, 1)
Menu.describe("restore_mode", "Normal: the car comes back stopped. Momentum: it keeps its speed.")

function onKeyDown(keyCode)
    local index, value = Menu.get("restore_mode")
end
```

### Função Global `print(...)`
- Todas as chamadas a `print(...)` em scripts Lua são interceptadas e redirecionadas para o sistema de log unificado do DR2Hook (`dr2hook.log`), adicionando prefixo `[Lua]`, nível de severidade e timestamping automático.

---

## 4. Recarregar sem fechar o jogo

Há dois reloads, e eles não fazem a mesma coisa.

| Ação | O que reinicia | O que permanece |
| :--- | :--- | :--- |
| **Reload Scripts (Hot-Reload)**, aba Mods | O estado Lua. `onInit` roda de novo. | A `dxgi.dll`, o core nativo e o checkpoint gravado em C++. |
| **F8** ou **Reload Native Core (F8)** | `dr2hook_core.dll` inteira: telemetria, overlay, savestate C++ e Lua. `onInit` roda de novo. | A `dxgi.dll`, os hooks de `Present` / `WndProc` e o `NetworkGuard`. |

Para testar uma DLL nativa nova, substitua `dr2hook_core.dll` na pasta do jogo e pressione **F8**. Não substitua `dr2hook_core.N.dll`: esse é o arquivo que o processo mantém mapeado. O checkpoint que só existia em memória é descartado. Variáveis Lua também. Trocar `dxgi.dll` ainda exige reiniciar o jogo.

---

## 5. Regras de Fair Play (Anti-Cheat Integrado)

O princípio nº 1 do DR2Hook é o **Fair Play First**:

1. **Isolamento de rede (Winsock), preso na `dxgi.dll`:**
   - `getaddrinfo` / `GetAddrInfoW` só resolvem localhost. Qualquer outro nome retorna “nome não encontrado”.
   - `connect` só aceita `127.0.0.0/8` e o loopback IPv6. O resto recebe `WSAECONNREFUSED` (`10061`).
   - Não há API de script para desligar isso, e o **F8** não desliga. O jogo volta a conectar depois que você sai e remove `dxgi.dll` e `dr2hook_core.dll`.
2. **Bloqueio Hard-Lock em C++:**
   - As funções `Player.setPosition`, `Player.setVelocity` e `Player.setState` validam a conexão e modo de jogo no núcleo nativo em C++ antes de qualquer escrita na memória.
   - Caso o jogador esteja em qualquer evento online oficial, a chamada **falhará e retornará `false`**, impedindo violações ou banimentos.
3. **Modos Liberados para Treino:**
   - **DirtFish** (Área Livre / Pista de Testes).
   - **Tomada de Tempo (Time Trial)**.
   - **Campeonatos Customizados Offline**.

---

## 6. Exemplo Completo: Mod de Treino (Practice Mode)

Abaixo está a implementação real do `mods/practice_mode/main.lua` incluído no pacote. As opções aparecem em **Pausa > DR2 Hook > aba Mods > Practice Mode**. As opções de pesquisa (número de carros fantasma, fantasma sólido, teclas F9 e F11) ficam no outro mod do pacote, `mods/debug_mode/main.lua` (**Debug Mode**).

```lua
-- ========================================================================
-- DR2 Hook: Practice Mode (Practice / Savestate Mod)
-- ========================================================================

local savedState = nil
local restoreModes = { "Normal", "Momentum" }

local function notify(text, duration)
    if Menu.get("notifications") then
        UI.notify(text, duration)
    end
end

local function saveCheckpoint()
    if Safety.isRestrictedMode() then
        notify("Savestate blocked in competitive/official modes!", 3.0)
        return
    end

    savedState = Player.getState()
    if savedState ~= nil then
        notify("Checkpoint saved!", 2.0)
        print(string.format("[Practice Mode] Checkpoint saved at: (%.2f, %.2f, %.2f)",
            savedState.position.x, savedState.position.y, savedState.position.z))
    else
        notify("Save failed: vehicle unavailable!", 2.5)
        print("[Practice Mode] Failed to capture vehicle state.")
    end
end

-- mode: "normal" (stationary) or "momentum"
local function restoreCheckpoint(mode)
    if Safety.isRestrictedMode() then
        notify("Restore blocked in competitive modes!", 3.0)
        return
    end

    if savedState == nil then
        notify("No checkpoint saved yet! Press F5 first.", 2.5)
        print("[Practice Mode] No saved checkpoint available.")
        return
    end

    local label = mode == "momentum" and "With Momentum" or "Normal"
    if Player.setState(savedState, mode) then
        notify("Returning to checkpoint (" .. label .. ")...", 1.5)
        print("[Practice Mode] Checkpoint restored (" .. mode .. ").")
    else
        notify("Failed to restore checkpoint in memory!", 2.5)
        print("[Practice Mode] Failed to apply vehicle state (" .. mode .. ").")
    end
end

local function selectedRestoreMode()
    local _, value = Menu.get("restore_mode")
    return value == "Momentum" and "momentum" or "normal"
end

-- Largada no reinicio. Os nomes do menu dizem o que acontece; Race.* usa os
-- ids internos. Reaplicada a cada especial, quando o modo da sessao ja e
-- conhecido; `quiet` evita avisos fora da especial.
local startModes = { "Normal", "No countdown", "Automatic", "On throttle" }
local startModeIds = {
    ["Normal"] = "normal",
    ["No countdown"] = "no_countdown",
    ["Automatic"] = "automatic",
    ["On throttle"] = "on_throttle",
}

local function applyStartMode(label, quiet)
    local mode = startModeIds[label] or "normal"
    if mode ~= "normal" and Safety.isRestrictedMode() then
        if not quiet then
            notify("Race start options blocked in competitive modes!", 3.0)
        end
        mode = "normal"
    end
    if not Race.setStartMode(mode) and mode ~= "normal" and not quiet then
        notify("Race start options unavailable (hook not loaded).", 3.0)
    end
end

local function selectedStartMode()
    local _, value = Menu.get("race_start")
    return value
end

-- Fantasma (GhostLab no core). Quantos carros fantasma, o fantasma solido e as
-- teclas de pesquisa ficam no mod Debug Mode.
local ghostShift = { "0 s", "+1 s", "+2 s", "+5 s", "-1 s", "-2 s", "-5 s" }

local function seconds(label)
    return tonumber((label or "0"):match("([+-]?%d+)")) or 0
end

local function applyGhostShift()
    local _, shift = Menu.get("ghost_time_shift")
    Ghost.setTimeOffset(seconds(shift))
end

-- Native menu: Pause > DR2 Hook > Mods > Practice Mode
Menu.button("save_checkpoint", "Save checkpoint", saveCheckpoint)
Menu.button("restore_checkpoint", "Restore checkpoint", function()
    restoreCheckpoint(selectedRestoreMode())
end)
Menu.choice("restore_mode", "Restore mode", restoreModes, 1)
Menu.toggle("clear_on_stage_start", "Clear checkpoint on new stage", true)
Menu.choice("race_start", "Race start", startModes, 1,
    function(_, value) applyStartMode(value, true) end)
Menu.toggle("ghost_gap", "Live gap to ghost", true,
    function(enabled) Ghost.setHud(enabled) end)
Menu.choice("ghost_time_shift", "Ghost head start", ghostShift, 1,
    function() applyGhostShift() end)
Menu.toggle("notifications", "Notifications", true)

-- Texto do painel da direita para a linha em foco.
Menu.describe("save_checkpoint",
    "Saves the car's position, rotation and speed right now. Shortcut: *F5*.")
Menu.describe("restore_checkpoint",
    "Puts the car back at the saved checkpoint, using the restore mode below. "
    .. "Shortcuts: *F6* (normal) and *F7* (with momentum).")
Menu.describe("restore_mode",
    "*Normal:* the car comes back stopped, with the suspension settled.\n"
    .. "*Momentum:* it keeps the speed and spin it had when saved.")
Menu.describe("clear_on_stage_start",
    "Forgets the checkpoint when a new stage starts. Restarting the same stage keeps it.")
Menu.describe("race_start",
    "How every start works, restarts included.\n\n"
    .. "*Normal:* hold the handbrake and wait for the 5 lights.\n"
    .. "*No countdown:* hold the handbrake and go at once.\n"
    .. "*Automatic:* go as soon as the car is on the line.\n"
    .. "*On throttle:* go when you press the throttle.")
Menu.describe("ghost_gap",
    "Shows at the top of the screen how far you are from the ghost.\n\n"
    .. "*Red:* seconds behind. *Green:* seconds ahead. The metres are the distance along the road.")
Menu.describe("ghost_time_shift",
    "Moves every ghost in time. *+2 s:* the ghost runs 2 seconds ahead of its real run.")
Menu.describe("notifications", "Shows on-screen messages for checkpoints and race start options.")

function onInit()
    applyStartMode(selectedStartMode(), true)
    Ghost.setHud(Menu.get("ghost_gap"))
    applyGhostShift()
    print("[Practice Mode] Loaded! Press F5 to save checkpoint, F6 to restore, F7 for momentum.")
end

function onStageLoad(stage)
    applyStartMode(selectedStartMode(), false)
end

function onStageStart(stage)
    if stage.restart then
        return
    end
    if Menu.get("clear_on_stage_start") then
        savedState = nil
    end
    print("[Practice Mode] New stage started: " .. stage.name)
end

function onKeyDown(keyCode)
    if keyCode == 0x74 then     -- F5: save car position and momentum
        saveCheckpoint()
    elseif keyCode == 0x75 then -- F6: restore stationary
        restoreCheckpoint("normal")
    elseif keyCode == 0x76 then -- F7: restore with full momentum
        restoreCheckpoint("momentum")
    end
end
```
