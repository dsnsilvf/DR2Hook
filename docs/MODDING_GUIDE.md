# Guia Oficial da API de Mods do DR2 ModLoader v0.1.0

Bem-vindo ao Guia Oficial de Desenvolvimento de Mods para o **DR2 ModLoader v0.1.0** no *DiRT Rally 2.0*.

O DR2 ModLoader oferece um ambiente de scripting modular e sandbox baseado em **Lua 5.4**, permitindo que a comunidade crie ferramentas de telemetria, modos de treino, interfaces personalizadas e automações em tempo real com overhead imperceptível de processamento (< 0.2ms por frame).

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
- **Quando é chamado:** Executado uma única vez assim que o mod é carregado pelo `ModManager` durante o boot do jogo ou recarregamento.
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
  - `0x70` a `0x7B`: Teclas de função `F1` a `F12` (`F5` = `0x74`, `F6` = `0x75`).
  - `0x2D`: Tecla `Insert` (reservada por padrão para o menu do DR2Hook).
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

### `onStageStart(stage)`
- **Quando é chamado:** Disparado ao iniciar ou reiniciar uma especial ou pista de treino.
- **Parâmetros:**
  - `stage` (table): Tabela contendo informações da especial, incluindo o campo `name` (string).
- **Assinatura:** `function onStageStart(stage)`
- **Exemplo:**
  ```lua
  function onStageStart(stage)
      print("[MeuMod] Nova especial iniciada: " .. tostring(stage.name))
  end
  ```

---

## 3. Tabela de APIs Nativas Expostas

O DR2 ModLoader expõe namespaces protegidos em C++ para o ambiente Lua:

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

### Módulo `UI`
Permite emitir mensagens e avisos visuais na tela através do sistema de notificações HUD do Dear ImGui.

| Função | Parâmetros | Retorno | Descrição |
| :--- | :--- | :--- | :--- |
| `UI.notify(message, duration)` | `message` (string), `duration` (number opcional, padrão `3.0`) | `nil` | Exibe uma notificação toast semi-transparente no canto superior da tela pelo tempo indicado (em segundos). |

### Função Global `print(...)`
- Todas as chamadas a `print(...)` em scripts Lua são interceptadas e redirecionadas para o sistema de log unificado do DR2Hook (`dr2hook.log`), adicionando prefixo `[Lua]`, nível de severidade e timestamping automático.

---

## 4. Regras de Fair Play (Anti-Cheat Integrado)

## 4. Regras de Fair Play (Anti-Cheat Integrado)

O princípio nº 1 do DR2 ModLoader é o **Fair Play First**:

1. **Isolamento Físico de Rede (Winsock Air-Gap):**
   - O `NetworkGuard` bloqueia conexões TCP e resoluções DNS para domínios da Codemasters/EA/RaceNet, garantindo que o jogo permaneça em modo puramente offline enquanto o mod loader estiver ativo.
2. **Bloqueio Hard-Lock em C++:**
   - As funções `Player.setPosition`, `Player.setVelocity` e `Player.setState` validam a conexão e modo de jogo no núcleo nativo em C++ antes de qualquer escrita na memória.
   - Caso o jogador esteja em qualquer evento online oficial, a chamada **falhará e retornará `false`**, impedindo violações ou banimentos.
3. **Modos Liberados para Treino:**
   - **DirtFish** (Área Livre / Pista de Testes).
   - **Tomada de Tempo (Time Trial)**.
   - **Campeonatos Customizados Offline**.

---

## 5. Exemplo Completo: Mod de Treino (Practice Mode)

Abaixo está a implementação real do `mods/practice_mode/main.lua` incluído no pacote:

```lua
-- ========================================================================
-- DR2 ModLoader: Practice Mode (Practice / Savestate Mod)
-- ========================================================================

local savedState = nil

function onInit()
    print("[Practice Mode] Loaded! Press F5 to save checkpoint, F6 to restore, F7 for momentum.")
end

function onStageStart(stage)
    -- Clear saved checkpoint when a new stage begins
    savedState = nil
    print("[Practice Mode] New stage started: " .. stage.name)
end

function onKeyDown(keyCode)
    -- F5 (0x74): Save car position and momentum
    if keyCode == 0x74 then
        if Safety.isRestrictedMode() then
            UI.notify("Savestate blocked in competitive/official modes!", 3.0)
            return
        end

        savedState = Player.getState()
        if savedState ~= nil then
            UI.notify("Checkpoint saved!", 2.0)
            print(string.format("[Practice Mode] Checkpoint saved at: (%.2f, %.2f, %.2f)", 
                savedState.position.x, savedState.position.y, savedState.position.z))
        else
            UI.notify("Save failed: vehicle unavailable!", 2.5)
            print("[Practice Mode] Failed to capture vehicle state.")
        end

    -- F6 (0x75): Restore position normally (stationary)
    elseif keyCode == 0x75 then
        if Safety.isRestrictedMode() then
            UI.notify("Restore blocked in competitive modes!", 3.0)
            return
        end

        if savedState ~= nil then
            local ok = Player.setState(savedState, "normal")
            if ok then
                UI.notify("Returning to checkpoint (Normal)...", 1.5)
                print("[Practice Mode] Checkpoint restored normally.")
            else
                UI.notify("Failed to restore checkpoint in memory!", 2.5)
                print("[Practice Mode] Failed to apply vehicle state.")
            end
        else
            UI.notify("No checkpoint saved yet! Press F5 first.", 2.5)
            print("[Practice Mode] No saved checkpoint available.")
        end

    -- F7 (0x76): Restore with full momentum
    elseif keyCode == 0x76 then
        if Safety.isRestrictedMode() then
            UI.notify("Restore blocked in competitive modes!", 3.0)
            return
        end

        if savedState ~= nil then
            local ok = Player.setState(savedState, "momentum")
            if ok then
                UI.notify("Returning to checkpoint (Momentum)...", 1.5)
                print("[Practice Mode] Checkpoint restored with momentum.")
            else
                UI.notify("Failed to restore checkpoint with momentum!", 2.5)
                print("[Practice Mode] Failed to apply vehicle state.")
            end
        else
            UI.notify("No checkpoint saved yet! Press F5 first.", 2.5)
            print("[Practice Mode] No saved checkpoint available.")
        end
    end
end
```
