# UI escondida, cortada e sem uso

O que existe nos dados da UI ou no executável e não aparece nos menus. Varredura de 2026-10-05, só leitura: `screens.bin`, `states.bin`, `flow.bin` ([UI Data](ui_data.md)), os `.lng`, `persistentDB.pssg` ([UI Render](ui_render.md)) e as strings do `dirtrally2.exe`. Nada foi aberto no jogo.

Marcadores: **CONFIRMADO** (visto nos dados), **PROVÁVEL**, **HIPÓTESE**.

## Método

1. **Telas órfãs:** `Screen` que nenhum estado do fluxo abre e cujo id não aparece em outra tela, num estado ou como string literal no executável. Correção de 2026-10-05: a primeira contagem (28) estava errada, porque contava o `SBAudioNotification screen_name` da própria tela como referência. A contagem certa está em [Situação das telas](#situação-das-telas).
2. **Estados fora do fluxo:** estado de `states.bin` sem nó em `flow.bin`.
3. **Alcance no fluxo:** busca a partir do `entrypoint boot`, seguindo os links do nó e dos ancestrais. Dá 6802 de 6809 nós alcançáveis, então quase nada está desligado do grafo; o que fica escondido depende de evento que nenhum item manda.
4. **Classes de estado sem uso:** nomes `State*` nas strings do executável que nenhum elemento de `states.bin` usa.
5. **Textos:** chaves do `language_eng.lng` que não aparecem nos dados nem no executável. A maioria (9353 de 12597) é montada em runtime (carros, locais, conquistas); só os grupos que indicam recurso cortado entram aqui.

### Formato `.lng` (`LNGT`)

Big-endian. CONFIRMADO: as 12597 chaves do inglês e as 12594 do português decodificam.

| Bloco | Conteúdo |
| :--- | :--- |
| `LNGT` + `u32` | Cabeçalho |
| `HSHS`, `HSHT` | Tabela de hash (não usada na leitura) |
| `SIDA` + `u32 tamanho` + `u32 n` | `n` pares `u32 offset da chave, u32 offset do valor` |
| `SIDB` + `u32 tamanho` | Chaves terminadas em zero; offsets relativos ao início dos dados |
| `LNGB` + `u32 tamanho` | Valores UTF-8 terminados em zero; offsets relativos ao início dos dados |

## Situação das telas

Contagem do visualizador ([UI Render](ui_render.md#visualizador-toolsuiview)), CONFIRMADA:

| Situação | Telas | O que quer dizer |
| :--- | ---: | :--- |
| Com estado no fluxo | 121 | Um estado de `flow.bin` abre a tela |
| Citada | 33 | Outra tela ou um estado cita o id (aba, bloco embutido) |
| Nome no executável | 80 | Só o `dirtrally2.exe` tem o id como string; o C++ pode abrir (abas de `tabs.info`, por exemplo) |
| Sem referência | 54 | Nenhuma das anteriores |

"Sem referência" **não prova** que a tela está morta. O grupo inclui telas que funcionam no jogo, como `results_hub`, `rh_*_standings`, `pvp_stage_results` e `rallyx_round_intro_*`, que o C++ deve abrir com nome montado em runtime. Duas telas estão marcadas nos próprios dados como sem uso: `skip_button` e `skip_button_data_enable` têm `glyph="_unused"`.

## Telas que nenhum estado abre

Existem em `screens.bin` e nenhum estado tem `screen_name` igual a elas. As três primeiras não têm referência em lugar nenhum. As três últimas têm o id como string no executável, então "cortada" é forte demais para elas: o C++ ainda pode abri-las.

| Tela | Conteúdo | Leitura |
| :--- | :--- | :--- |
| `hard_currency_store` | Título "Store" e 14 botões `store_button_N`, visíveis por `view[N].is_valid`, todos com evento `purchase` e sem texto ligado | Loja de moeda premium (microtransação). Sem referência |
| `subscription_store` | Só o título `lng_subscription_store_title` | Assinatura. Textos soltos: "DIRT PLUS" / "PURCHASE DIRT PLUS MEMBERSHIP". Sem referência |
| `team_offer_select` | Combo "Team Offer" (`offers_index`), Selecionar, Voltar; título herdado "/ HISTORIC CHAMPIONSHIP" | Ofertas de equipe da carreira; textos `lng_team_offers*`. Sem referência |
| `store_hub` | Dois tiles: Loja (`select_value=store`) e Anúncios (`announcements`) | Hub de loja. Nome no executável |
| `search_results` | Navegador de sessões: Host, Disciplina, Próximo estágio, Jogadores; 7 linhas `session[N]`; Ver perfil, Atualizar | Busca de campeonatos online. Nome no executável |
| `search_filters` | Combos: Mostrar em andamento, Disciplina, Local, Assistências, Participantes | Filtros da busca acima. Nome no executável |

Na área de serviço há também um item com chave sem tradução, `lng_challenge_hub` (aparece em vermelho no visualizador).

As abas `challenge_select_*_tab`, `scenario_select_*_tab` e `tuning_*` têm o nome no executável: o C++ as cria em `tabs.info` ([UI Tabs](ui_tabs.md)). Não estão cortadas.

## Estados fora do fluxo

CONFIRMADO, 8 de 737: um `GameState` sem atributos, `StateNetPrivilegeCheck type=voice`, `StateNetSystemMessage` `ChatRestriction` e `UGCRestriction`, `StateScreenFadeOutReplay`, `StateRallycrossPlayerGroupDecision`, `StateTerminalDamageNonCinematicEnd` e `StateAudioEvent replay_music_start`. Nenhum abre tela.

`StateTerminalDamageNonCinematicEnd` é o fim do dano terminal sem a cutscene ([Dano terminal](terminal_damage.md)).

## Classes de estado registradas e sem uso

102 nomes `State*` no executável não aparecem em `states.bin`. Parte são classes base. Os que indicam recurso:

| Grupo | Classes | Leitura |
| :--- | :--- | :--- |
| Desenvolvimento | `StateScreenTestImgui`, `StateImageDownloadTest`, `StateLeaderboardTest` | Testes. O ImGui não está no binário (só o nome da classe e a string `imgui`) |
| Depuração de corrida | `StateSwapControllerToAI`, `StateSwitchPlayerVehicle`, `StateTeleportVehicleToGrid`, `StatePreraceDriveToLine` | IA assume o carro, trocar de carro, teleportar para o grid. HIPÓTESE: o código pode funcionar se um estado for criado nos dados |
| Pintura | `StateScreenColourEditBase` | Editor de cores. O compositor `livery_editor_*` existe e monta as pinturas; texto solto `lng_dev_livery_editor = "DEV LIVERY EDITOR"` |
| Replays | `StateReplaySelect`, `StateInstantReplayScreen`, `StateProceduralInstantReplay` | Escolher replay salvo, replay instantâneo |
| Herança do DiRT 4 | `StateJoyride*` (6), `StateCheckJoyrideChapter`, `StateLandrushSetupForNextStage`, `StateHillClimbCareerStep` | Modos Joyride, Landrush e carreira de subida de montanha |
| Telas antigas | `StateScreenBasicGraphics`, `StateScreenAdvancedGraphics`, `StateScreenGarage`, `StateScreenInventory`, `StateParts`, `StatePartFailure`, `StateVehiclePrepOverview`, `StateScreenRankedLeaderboard`, `StateScreenRankedPerformance` | Substituídas por estados genéricos ou abas |
| Outros | `StateBidstackEndSession`, `StateIsBrandingMode`, `StateNetMatchmaking`, `StateCustomLobby`, `StateTutorialCheck`, `StateTierIntro` | Anúncios (Bidstack), modo de marca, matchmaking |

## Textos de recursos cortados

Chaves sem nenhuma referência nos dados nem no executável. CONFIRMADO que existem; o uso em runtime não foi descartado para todas.

| Grupo | Exemplos | Leitura |
| :--- | :--- | :--- |
| Benchmark | `lng_gfx_benchmark_hub_title "BENCHMARK"`, `lng_gfx_benchmark_button_title "Run Benchmark Test"`, `lng_benchmark_summary_*` (FPS médio/mín./máx.) | A entrada nas opções gráficas foi cortada. O modo funciona por linha de comando: `-benchmark example_benchmark.xml`, arquivo na pasta do jogo |
| DiRT Plus | `lng_subscription_tile_title "DIRT PLUS"` | Assinatura (ver `subscription_store`) |
| Biblioteca de vídeos | `lng_video_library_title "VIDEO LIBRARY"`, vídeos de introdução e de campeão (Rally, Rallycross, Historic, Landrush, Joyride) | Do DiRT 4 |
| Landrush | 93 chaves, ex.: `lng_baja_landrush_r_0_caps "FULL CIRCUIT"`, `nevada_landrush_*` | Do DiRT 4 |
| Joyride | `lng_joyride_results_chapter_hub_label "SELECT CHALLENGE"`, medalhas | Do DiRT 4 |
| Patrocínio | 50 `lng_brand_affinity_*` ("Increases the Sponsor 'Negotiation' Skill…") | Gestão de equipe do DiRT 4 |
| Texto de dev | `lng_ghost_validation_failure_title "[DEV TEXT] Ghost Validation Failed! ERROR CODE …"`, `lng_career_online_*_reset_dialog_dev_body "[DEV TEXT] My Team … championship reset"`, `lng_enable_test_test "TEST"` | Diálogos internos. O de validação de fantasma interessa a [Fantasmas](ghosts.md) |
| Pinturas | `lng_plain_colour_team_*`, 112 `lng_licensed_livery_colours_*` ("Privateer") | Nomes de cor de pintura, provavelmente montados por id |

## O que não está escondido

- **Percurso livre (DirtFish):** `free_roam_conditions_select` e `vehicle_select_free_roam` abrem pelo tile `free_roam` de `other_modes`. É modo real, de um patch.
- **Ranked:** `ranked_hub` e as telas `ranked_*` estão no fluxo depois de `StateEgoNetSignIn`. Não verifiquei se o servidor ainda responde.

## Próximos passos

| # | Ideia | Como |
| :--- | :--- | :--- |
| 1 | Abrir `search_filters`/`search_results` ou `hard_currency_store` | Estado `StateScreenFECore` com `screen_name` e um link no fluxo, como as telas do DR2 Hook ([UI Limits](ui_limits.md)). Sem C++ por trás, os dados `view[N]`/`session[N]` ficam vazios |
| 2 | Testar `StateTeleportVehicleToGrid` ou `StateSwapControllerToAI` | Registrar um estado com essa classe em `states.bin` e ligá-lo a um evento da pausa. Risco de travar; testar fora de evento online |
| 3 | `StateReplaySelect` | Ver se o jogo grava replays em disco antes; o estado sozinho não traz tela |
| 4 | `lng_ghost_validation_failure_*` | Achar quem usa o código de erro: diz quando o jogo rejeita um fantasma |
