# Guia Oficial da API de Mods do DR2Hook

Bem-vindo ao Guia Oficial de Desenvolvimento de Mods para o **DR2Hook** no *DiRT Rally 2.0*.

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

O DR2Hook expõe namespaces protegidos em C++ para o ambiente Lua:

### Módulo `Player`
Controla leitura e escrita de cinemática e suspensão do veículo do jogador.

| Função | Parâmetros | Retorno | Descrição |
| :--- | :--- | :--- | :--- |
| `Player.getPosition()` | Nenhum | `table` ou `nil` | Retorna `{ x = number, y = number, z = number }`. |
| `Player.setPosition(pos)` | `pos` (table: `{x, y, z}`) | `boolean` | Altera a posição tridimensional do veículo na pista. |
| `Player.getVelocity()` | Nenhum | `table` ou `nil` | Retorna velocidades nos eixos cartesianos `{ x, y, z, vx, vy, vz }`. |
| `Player.setVelocity(vel)` | `vel` (table: `{vx, vy, vz}`) | `boolean` | Altera a velocidade linear do veículo. |
| `Player.getState()` | Nenhum | `table` ou `nil` | Captura o estado completo de física do veículo (ver estrutura abaixo). |
| `Player.setState(state)` | `state` (table) | `boolean` | Restaura estado completo com sanitização física e suspensão normalizada. |

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

O princípio nº 1 do DR2Hook é o **Fair Play First**:

1. **Bloqueio Hard-Lock em C++:**
   - As funções `Player.setPosition`, `Player.setVelocity` e `Player.setState` validam a conexão RaceNet e o modo de jogo no núcleo nativo em C++ antes de qualquer escrita na memória.
   - Caso o jogador esteja em **Carreira Online / My Team**, **Desafios Diários/Semanais/Mensais** ou **Clubes Oficiais Ranqueados**, a chamada **falhará silenciosamente e retornará `false`**, impedindo penalidades de desclassificação ou banimentos da conta do usuário.
2. **Boas Práticas de Implementação:**
   - Sempre consulte `Safety.isRestrictedMode()` antes de disparar alterações de física ou savestates:
   ```lua
   if Safety.isRestrictedMode() then
       UI.notify("Ação bloqueada pelo Fair Play em eventos oficiais!", 3.0)
       return
   end
   ```
3. **Modos Liberados para Treino:**
   - **DirtFish** (Área Livre / Pista de Testes).
   - **Tomada de Tempo (Time Trial)**.
   - **Campeonatos Customizados Offline**.

---

## 5. Exemplo Completo: Mod de Treino (Practice Mode)

Abaixo está a implementação real do `mods/practice_mode/main.lua` incluído no pacote:

```lua
local savedState = nil

function onInit()
    print("[Practice Mode] Carregado! Pressione F5 para salvar o checkpoint e F6 para restaurar.")
end

function onStageStart(stage)
    savedState = nil
    print("[Practice Mode] Nova especial iniciada: " .. tostring(stage.name))
end

function onKeyDown(keyCode)
    -- F5 (0x74): Gravar Checkpoint
    if keyCode == 0x74 then
        if Safety.isRestrictedMode() then
            UI.notify("Savestate bloqueado em modos competitivos/oficiais!", 3.0)
            return
        end

        savedState = Player.getState()
        UI.notify("Checkpoint salvo!", 2.0)
        print(string.format("[Practice Mode] Checkpoint gravado em: (%.2f, %.2f, %.2f)", 
            savedState.position.x, savedState.position.y, savedState.position.z))

    -- F6 (0x75): Restaurar Checkpoint
    elseif keyCode == 0x75 then
        if Safety.isRestrictedMode() then
            UI.notify("Restauração bloqueada em modos competitivos!", 3.0)
            return
        end

        if savedState ~= nil then
            Player.setState(savedState)
            UI.notify("Retornando ao checkpoint...", 1.5)
        else
            UI.notify("Nenhum checkpoint salvo ainda! Pressione F5 primeiro.", 2.5)
        end
    end
end
```
