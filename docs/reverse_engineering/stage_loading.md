# Stage Loading & Fast-Path Architecture (EGO Engine / DiRT Rally 2.0)

Este documento descreve a arquitetura de resolução e carregamento de especiais (pistas/traçados) no *DiRT Rally 2.0* (`dirtrally2.exe`), os pontos de interceptação na máquina de estados de interface do motor e o funcionamento do subsistema `AutoStage` do DR2Hook.

---

## 1. Visão Geral da Resolução de Pistas

No EGO Engine, uma especial não é identificada apenas por um ID numérico, mas por um descritor de tupla com strings fixas em buffers alinhados de 32 bytes (`char[32]`):

```cpp
struct StageDescriptor {
    char location[32];         // +0x00: ex: "usa", "new_zealand", "spain", "uk"
    char track[32];            // +0x20: ex: "twin_peaks", "new_zealand_rally_01", "wales_rally_01"
    char route[32];            // +0x40: ex: "free_roam", "route_1", "route_2"
    char time_of_day[32];      // +0x60: ex: "midday", "sunset", "morning", "night", "dusk"
    char surface_weather[32];  // +0x80: ex: "dry", "wet"
    char vehicle[32];          // +0xa0: ex: "fr5", "555", "22b", "c3r", "fab"
    char livery[32];           // +0xc0: ex: "00", "01"
    uint32_t flags;            // +0xf0: 1
};
```

### 1.1 Localidades e Pacotes NEFS

Os arquivos de localidade residem no diretório `/locations/` do jogo no formato `<location>__<track>.nefs`:
- DirtFish: `locations/usa__twin_peaks.nefs`
- Nova Zelândia: `locations/new_zealand__new_zealand_rally_01.nefs`
- Estados Unidos: `locations/usa__usa_rally_01.nefs`
- Espanha: `locations/spain__spain_rally_01.nefs`
- Austrália: `locations/australia__australia_rally_01.nefs`
- Polônia: `locations/poland__poland_rally_01.nefs`
- Argentina: `locations/south_america__argentina_rally_01.nefs`
- País de Gales: `locations/uk__wales_rally_01.nefs`
- Escócia: `locations/uk__scotland_rally_03.nefs`

### 1.2 Pipeline de Resolução de Caminhos

A rotina em `0x1403a3400` formata internamente os seguintes identificadores virtuais:
1. `tracks/locations/%s/%s/%s` $\rightarrow$ Geometria da rota, malha de colisão física e setores.
2. `tracks/locations/%s/%s/trees` $\rightarrow$ Vegetação e posicionamento de árvores.
3. `tracks/locations/%s/%s` $\rightarrow$ Shaders, materiais e texturas do traçado.
4. `tracks/locations/%s/generic` $\rightarrow$ Skybox, iluminação ambiental e terreno base da região.

Dentro do pacote NEFS da especial, o motor processa:
- `track_spline.xml`: A spline 3D central da estrada (orientação, curvatura e linha de referência para cálculo de tempo e IA).
- `progress_track.xml`: Coordenadas de largada, portais de tempo intermediários (splits) e linha de chegada.
- `surface_deg.xml`: Coeficientes de desgaste e aderência da pista.
- `baked_lights.pssg`: Iluminação pré-calculada para o horário selecionado.

---

## 2. O Subsistema Fast-Path (`BenchmarkManager`)

O motor contém um subsistema nativo de benchmark projetado para automação e testes de desempenho internos. Esse gerenciador é uma instância estática única localizada no BSS em:

- **Endereço Virtual (VA)**: `0x141f59720` (RVA `0x1f59720` a partir da base `0x140000000`).

### 2.1 Layout de Memória do `BenchmarkManager`

| Offset | Tipo | Nome | Descrição |
|---|---|---|---|
| `+0x00` | `char[64]` | `outputfilename` | Prefixo do relatório de benchmark (ex: `"DiRT_Benchmark"`). |
| `+0x40` | `char[64]` | `hardwaresettings` | Arquivo XML de perfil gráfico. |
| `+0x80` | `uint32_t` | `benchmark_enabled` | Flag mestra: se `1`, ignora todos os menus do frontend. |
| `+0x84` | `uint8_t` | `flag_84` | Flag de inicialização interna. |
| `+0xa0` | `uint8_t` | `flag_a0` | Flag de sincronização gráfica. |
| `+0xa1` | `uint8_t` | `cycle_camera` | `0` = mantém câmera de jogabilidade; `1` = cicla câmeras externas. |
| `+0xa4` | `uint32_t` | `cycle_period` | Intervalo em segundos entre trocas de câmera. |
| `+0xa8` | `char[32]` | `location` | Região (ex: `"usa"`, `"new_zealand"`). |
| `+0xc8` | `char[32]` | `track` | Pista (ex: `"twin_peaks"`, `"new_zealand_rally_01"`). |
| `+0xe8` | `char[32]` | `route` | Traçado (ex: `"free_roam"`, `"route_2"`). |
| `+0x108` | `char[32]` | `time_of_day` | Horário (ex: `"midday"`, `"sunset"`, `"night"`). |
| `+0x128` | `char[32]` | `surface_weather` | Condição de piso (ex: `"dry"`, `"wet"`). |
| `+0x148` | `char[32]` | `vehicle` | Identificador do carro (ex: `"fr5"`). |
| `+0x168` | `char[32]` | `livery` | Número da pintura (ex: `"00"`). |
| `+0x198` | `uint32_t` | `active` | Estado ativo da sessão de teste (`1`). |

---

## 3. `BenchmarkManager::Initialize` (`0x1409d31c0`) — verificado por disassembly (2026-10-01)

Chamado uma única vez por `0x14038fc20`, dentro do construtor da aplicação `0x14038f740` (início do `WinMain`). Assinatura: `(this=0x141f59720, argv: std::vector<std::string>*, r8, r9)`.

Fluxo real:
1. `memset(this+0xa8, 0, 0xf8)`; `+0x1a0 = 0`; `+0x88 = r8`, `+0x90 = r9`; `+0xa0 = 0x0100` (`flag_a0=0`, `cycle_camera=1`); `+0xa4 = 20`; copia `"Benchmark"` → `+0x00` e `"hardware_settings_config.xml"` → `+0x40`.
2. Varre o argv procurando **`-benchmark`**. Se achar: `+0x80 = 1` e chama `0x1409d4250(this, próximo_arg)` (parse do XML); se falhar, tenta `example_benchmark.xml`.
3. Se `+0x80 != 0`: chama `0x1409d0340` (cria `<Documentos>\benchmarks`) e escreve a especial **hardcoded**: `new_zealand / new_zealand_rally_01 / route_2 / midday / dry / fr5 / 00`, `+0x198 = 1`.

Ou seja: o modo é nativo e acessível por linha de comando (`-benchmark example_benchmark.xml`); o XML **não** escolhe pista — só nome do relatório, hardware settings e câmera.

Getters dos campos (consumidores leem depois via estes, então sobrescrever após o Initialize tem efeito):
`0x1409d2c10`→`+0xa8` location, `0x1409d3010`→`+0xc8` track, `0x1409d2d20`→`+0xe8` route, `0x1409d2a40`→`+0x148` vehicle, `0x1409d2c00`→`+0x168` livery (formatados como `"%s_%s"` em `0x1405b5cfc`).

### 3.1 Consumidores de `+0x80`
~21 leituras de `BM+0x80` no `.text` (não 6): `0x14027ac22`, `0x14027f63a`, `0x140281ac4`, `0x1402a670f`, `0x1402a7a58`, `0x1402a996a`, `0x14037b0de`, `0x1403b9df8`, `0x1403ba60d`, `0x14047b745`, `0x14047d7b0`, `0x1404b5af7`, `0x14051f104`, `0x14051f694`, `0x1405293ef`, `0x1405620a7`, `0x14056a00a`, `0x1409b89b5`, `0x1409c7beb`, `0x140a62897`, `0x140a62d29`. A semântica de cada desvio **não** foi verificada (ex.: em `0x14056a00a` o bloco grande roda quando o benchmark está *desligado*).

### 3.2 Hipótese aberta: o carro é dirigido por uma gravação
`0x140551200` constrói um `RaceRecorderDataLoader` com o recurso `"benchmark"`, e há um pool de memória `"benchmark"` em `0x140427713`. Hipótese: o benchmark reproduz uma corrida gravada para NZ/route_2/fr5 — o jogador pode não ter controle, e trocar a pista pode dessincronizar ou quebrar a reprodução. **Validar no jogo antes de tratar o AutoStage como "carregar especial jogável".**

**Confirmado no jogo (2026-10-01 19:49, teste 1):** com `-benchmark example_benchmark.xml` e AutoStage desligado, o jogo vai direto para a especial de NZ, com copiloto falando e um carro de demonstração dirigindo sozinho. Os hooks do DR2Hook (telemetria/overlay) tratam esse carro como o do jogador. Ou seja, o benchmark serve para carregar uma especial sem passar pelos menus, mas **não** dá controle ao jogador.

---

## 4. Hook do `AutoStage` no DR2Hook

- Alvo: `0x1409d31c0`, prólogo `48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 56`.
- Instalado no `DLL_PROCESS_ATTACH` (`src/core/main.cpp`), igual ao hook do UiData. `dxgi.dll` entra no processo como dependência de `d3d11.dll` (import estático do exe), então o `DllMain` roda antes do `WinMain`.
- Detour: chama o original, guarda se o `-benchmark` nativo estava ativo, e se o ini estiver ativo sobrescreve `+0x80`, `+0xa1`, strings e `+0x198`. Quando o modo foi forçado (sem `-benchmark`), também chama `0x1409d0340` para criar a pasta como o fluxo nativo faria.
- Logging: o Logger só abre na thread de init. `LogAutoStageStatus()` (chamado após `Logger::Init`) relata a instalação e o resultado do detour, rode ele antes ou depois.

---

## 5. Falha do primeiro teste (race condition) — corrigida

Log de 19:26: hook instalado às `00.401`, logger aberto às `59.855`, nenhuma execução do detour. O hook era instalado no fim da `DR2Hook_InitThread`, depois de criar o swapchain D3D11 de teste (~0,5 s), e o `Initialize` já tinha rodado no `WinMain`. O horário exato da chamada nativa não foi medido; a ordem é inferida da estrutura (chamada única no construtor da aplicação).

Correção aplicada: instalação movida para o `DllMain`.

---

### 5.1 Crash com override de pista (2026-10-01 19:45)
Com a instalação já corrigida, `enabled = 1` + `usa / twin_peaks / free_roam` e sem `-benchmark`: o detour rodou e aplicou o override, e ~2 s depois o jogo crashou em `0x140439044` (`mov rax,[rcx]` com `rcx = [rdi+0x32a0]` nulo, função `0x140438d52`) enquanto carregava configurações de carro e efeitos. O teste 2 descartou o modo forçado (NZ/route_2 forçado pelo hook carrega normal). A causa provável é a troca de pista com a gravação de NZ; o que é o objeto em `+0x32a0` ainda não foi identificado.

Efeito colateral confirmado: a chamada a `0x1409d0340` criou `Documents\My Games\DiRT Rally 2.0\benchmarks`.

## 6. Próximos testes (em ordem)

1. ✅ **Baseline nativo** (feito, ver 3.2): `enabled = 0` no ini, opção de inicialização Steam `-benchmark example_benchmark.xml`. Observar: vai direto para NZ route_2? Quem dirige? O que acontece no fim (relatório em `Documentos\My Games\DiRT Rally 2.0\benchmarks`, volta ao menu, fecha)?
2. ✅ (feito 2026-10-01: carregou igual ao teste 1 → o hook força o modo sem problema; o crash de 5.1 vem da troca de pista) **Override com a pista nativa**: `enabled = 1`, NZ/route_2/fr5, sem `-benchmark`. Confirma que o hook roda (log: `-benchmark nativo = nao, override aplicado = sim`) e que o resultado é igual ao teste 1.
3. **Override de pista**: `usa / twin_peaks / free_roam`. Só depois de 1 e 2.

---

## 7. Medição de tempo de carregamento (LoadTrace, 2026-10-01)

`src/core/load_trace.cpp` (no `dxgi.dll`) loga aberturas de `locations/*.nefs`/`tracks`/`cars`, MB lidos por segundo (`ReadFile`) e frames > 100 ms. Início do carregamento = abertura do `.nefs` da localidade; fim = último segundo com leitura relevante.

| Execução | Carro | Abre `.nefs` | Fim da leitura | Disco ativo |
|---|---|---|---|---|
| Normal, Finlândia (Kontinjärvi) | gti | 20:05:15.5 | 20:05:26.9 | ~11,3 s |
| Normal, Hawkes Bay (Te Awanga Sprint Forward) | gti | 20:12:31.7 | 20:12:39.8 | ~8,1 s |
| Normal, Hawkes Bay, reinício | gti | ~20:13:11.9 | 20:13:19.0 | ~7,1 s |
| Benchmark, Hawkes Bay | fr5 | 20:14:51.1 | 20:14:58.7 | ~7,6 s |

Leitura contínua (80–430 MB/s) em todos, sem segundos ociosos. **O carregamento em si dura o mesmo nos dois modos.** O benchmark é mais rápido porque começa a carregar 2,6 s após o processo abrir (sem menus) e não para na tela de informações da especial. A hipótese de espera artificial da UI, se existir, está **depois** do fim da leitura (tela de informações / "continuar"), não durante o carregamento. Próximo passo: marcar no log o momento em que a largada fica jogável.

Nomes amigáveis das rotas (decodificados de `language/language_eng.lng` em `game_1.dat`, tabela `SIDA` de pares `(offset_chave, offset_valor)` big-endian sobre os blocos `SIDB`/`LNGB`):
`new_zealand_rally_01`: route_0 Te Awanga Forward, route_1 Ocean Beach, **route_2 Te Awanga Sprint Forward** (benchmark), route_3 Ocean Beach Sprint Forward, route_4 Ocean Beach Sprint Reverse, route_5 Te Awanga Sprint Reverse.
