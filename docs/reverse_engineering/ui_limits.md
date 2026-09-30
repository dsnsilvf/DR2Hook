# Limites e capacidades da UI nativa

Esta página mapeia até onde a UI do frontend vai só com dados (`screens.bin`, `states.bin`, `flow.bin`, `message_dialogs`) e onde ela passa a depender de C++. O formato dos arquivos está em [ui_data.md](ui_data.md). O menu de pausa ao vivo está em [menu.md](menu.md).

Nada foi escrito no jogo nem no processo. As fontes são os dados extraídos com `tools/egodata`, as cenas PSSG de `frontend/databases/persistentDB.pssg` e a desmontagem estática do `dirtrally2.exe` (base `0x140000000`). Os scripts de análise ficaram em `/tmp/ui_limits`.

Marcadores: **CONFIRMADO** (visto nos dados, no código ou no jogo), **PROVÁVEL** (indício forte, sem teste), **HIPÓTESE** (a testar).

## Resumo dos limites

| Pergunta | Resposta | Grau |
| :--- | :--- | :--- |
| Quantas linhas o `smart_screen` mostra? | 11 (`smart_set` tem os nós `0` a `10` dentro de um `UINODESCROLL`). O resto rola. | CONFIRMADO (cena + captura de "Gráficos avançados") |
| Quantos itens uma lista do `smart_screen` aceita? | O maior uso vanilla é 29 itens + `apply` (`advanced_graphics`). Não há teto visto nos dados. | CONFIRMADO o uso; teto HIPÓTESE |
| `smart_set.11` existe? | Não como nó da cena. Índices acima dos nós declarados funcionam no jogo (pausa usa `.9` e `.10` num molde com `0` a `7`). | CONFIRMADO o fato; mecanismo UNKNOWN |
| Quantas linhas o `smart_hub` mostra? | O molde tem 8 posições e não tem rolagem. A pausa usa 11. | CONFIRMADO; limite visual HIPÓTESE |
| Menu de contexto | 8 opções (`smart_context_panel.0` a `.7`) | CONFIRMADO (cena) |
| Listas de resultado | 12 linhas por página, com `scroller` | CONFIRMADO |
| Dropdown ou lista suspensa | Não existe. Opções múltiplas são combos que giram com esquerda/direita (`IBComboText*`). | CONFIRMADO (nenhum behaviour de dropdown registrado) |
| Diálogo sim/não sem C++ | Possível: `GameState` + `TaskDisplayDialog id=<mensagem>`, e as opções viram links do fluxo. | CONFIRMADO nos dados vanilla |
| Texto longo | Não há reticências. O padrão de todos os estilos é `clampuniform` (encolhe). Há `wordwrap` e `truncate`. | CONFIRMADO (enum + `text_styles.xml`) |
| Maiúsculas | Não há conversão em tempo de execução para `localise_upper`: o jogo procura a chave `<chave>_caps`. | CONFIRMADO |
| Painel de descrição por item | Na maioria das telas é estático. Mudar por item exige dado escrito pelo C++ (ou pela DLL). | CONFIRMADO |
| Toggle/slider no `smart_hub` | Não funciona: esses objetos não estão na biblioteca do hub. | CONFIRMADO (estático) |

## Objetos de tela

`screens.bin` tem 288 `Screen` e 61 `World`. O atributo `object` escolhe a cena `fe/screens/<pasta>/<object>` no `persistentDB.pssg`. Os glyphs são caminhos com ponto dentro dessa cena.

| `object` | Telas | Itens por tela | Posições na cena | Rolagem | Fluxo usado |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `smart_screen` | 26 | até 31 | `smart_set.0` a `.10` | Sim (`smart_set.scroller`) | `SBScrollableItemFlow` ou fluxos dinâmicos |
| `smart_screen_tabbed` | 15 | 0 ou 1 | — | — | `SBTabGroup` (abas vêm de `tabs.info`, C++) |
| `smart_hub` | 2 (`options_ingame`, `pause_menu`) | 4 a 11 | `smart_set.0` a `.7` | Não | `SBGridItemFlow` |
| `service_area` | 1 | 18 | `smart_hub_set.0` a `.7` | Não | `SBGridItemFlow` |
| `tuning` | 6 | — | `smart_set.0` a `.12` | — | `IBItemFlowIndex` + descrição dinâmica |
| sem `object` | 37 | — | — | — | `layer="context"` e afins |

Atributos de `Screen` lidos pelo parser em `0x140d24200`–`0x140d24500` (referências a strings, CONFIRMADO): `glyph`, `layer`, `object`, `data_parent_override` (`0x140d243b5`), `id`, `type`. Valores de `layer` presentes no mesmo trecho: `screen`, `context`, `dialog`, `overlay`. A raiz de dados é montada com o formato `ui.%s`.

Camadas sem objeto próprio:

- 26 menus de contexto usam `glyph="context_menu"`, com `smart_context_panel.0` a `.7`.
- `dialog` usa `layer="dialog"` e glyph `popup`. `foreground` usa `layer="overlay"`.
- `t_and_c_popup` e `popup_repairs` são popups dedicados.

Listas de resultado (`results_set`, `smart_lead_set`, `r_orders_set`) têm as posições `0` a `11` e um `scroller`.

86 telas não são referenciadas por nenhum estado. São abas (`SBScreenTabControl tab screen_id`, `SBTabGroup`) e blocos embutidos. `basic_graphics` e `advanced_graphics` estão entre elas. As classes `StateScreenBasicGraphics` e `StateScreenAdvancedGraphics` estão registradas e nenhum estado dos dados as usa.

## Objetos de item e posições

### Bibliotecas

Cada item desenha um `BSwitchStatic object="<nome>"`. O nome precisa existir na biblioteca da cena.

| Biblioteca | Objetos |
| :--- | :--- |
| `smart_hub_button_library` | `smart_hub_button_5col`, `smart_hub_button_alert_5col` |
| `smart_library` (25) | `button`, `button_14col`, `button_15col`, `button_entry_14col`, `button_entry_15col`, `toggle_14col`, `toggle_15col`, `toggle_24col`, `toggle_flag_14col`, `toggle_flag_15col`, `toggle_soft_lock_15col`, `nub_slider_labelled_14col`, `nub_slider_labelled_15col`, `fill_slider_labelled_14col`, `fill_slider_labelled_15col`, `tuning_slider_labelled_15col`, `difficulty_slider_15col`, `heading_14col`, `heading_15col`, `label_15col`, `smart_pill_header_15col`, `input_header`, `input_bindings`, `input_bindings_button`, `input_device_15col` |

Outros: `apply_button` fica em `smart_set.switch_footer` (xref `smart_confirm_button`). `smart_divider_24col` usa o glyph `1.switch`.

Usos nos dados: `toggle_15col` 120, `tuning_slider_labelled_15col` 45, `nub_slider_labelled_15col` 36, `button_15col` 19.

CONFIRMADO: um objeto ausente falha. A tabela de erros do switch (strings em `0x1413d9d58`–`0x1413d9e80`, seletor em `0x140d51a00`/`0x140d51aa0`) contém "Required child node was not found in switch" e "Couldn't construct glyph for required child". Por isso toggle, slider e combo só servem em `smart_screen`. No `smart_hub` o único molde tem um texto (`text_title`), e o valor precisa ir dentro do rótulo.

### Cenas (PSSG)

Tipos de nó de UI e atributos relevantes:

| Nó | Atributos | Uso |
| :--- | :--- | :--- |
| `UINODESTACKER` | `ty`, `ug`, `gs` | Empilha filhos |
| `UINODESCROLL` | `csd`, `csm`, `css`, `cx`, `cy` | Recorte e rolagem |
| `UINODETEXT` | `sn` (estilo), `wi` (largura), `ha`, `wr` (quebra) | Texto |
| `UINODEANIMATED` | `xr` (cena de componente) | Inclui outro componente |
| `UINODESWITCH` | `ir` | Troca de filho |
| `FENODECOUNT` | `c` | Contagem de nós da cena, não declara réplicas |

Posições por componente (CONFIRMADO, lidas da cena):

- `fe/screens/smart_hub/smart_hub` → `smart_set` com `xr=fe/component/smart_hub/smart_hub_set_pause`: um stacker (`ty=2`, `gs=0`) com os filhos `0` a `7` e sem `UINODESCROLL`.
- `fe/component/smart_hub/smart_hub_set` (usado por `service_area`): também `0` a `7`. A tela `service_area` declara itens até `smart_hub_set.17`.
- `fe/component/smart/smart_set`: `button_stacker > switch_footer > apply_button` e `scroller` (`UINODESCROLL`, `cy=1`) `> stacker > 0..10`.
- `fe/screens/smart/smart_screen`: `null_trans_secondary/smart_set`, `null_trans_main/smart_contextual_info`, `smart_sidebar` e `null_trans_title/screen_header`.

**A pergunta do `smart_set.11`.** A cena não tem nó `11`. Mesmo assim, a pausa declara `smart_set.9` e `.10` num molde com `0` a `7`, e o [menu.md](menu.md) mostra os itens 9 e 10 desenhados no jogo. CONFIRMADO: índices acima dos nós da cena funcionam. PROVÁVEL: o stacker gera filhos em tempo de execução a partir do primeiro. O mecanismo não foi achado. PROVÁVEL: `smart_set.11+` também funciona. No `smart_hub`, sem rolagem, o limite visível é a altura da tela (HIPÓTESE).

Correção para [ui_data.md](ui_data.md): o texto "`smart_set.0` a `.7`, as 8 posições do `smart_hub`" descreve o molde, não o limite. A pausa vanilla já usa 11.

**Quantas linhas o `smart_screen` mostra.** 11. A cena tem `0` a `10` dentro do scroller, e a captura de "Gráficos avançados" mostra 11 linhas mais o botão APLICAR. `advanced_graphics` tem 29 itens + `apply`. Em "Gráficos básicos" aparecem 8 linhas porque a tela só tem 8 itens.

### Texto dos fluxos (correção)

CONFIRMADO no `screens.bin`: os 80 textos de fluxo separam as linhas com `\r\n`. 66 são listas com um id por linha e 14 são grades com colunas, como `spare_new0 spare_new1 spare_new2 spare_new3` ou `hist_champ custom time_trial` / `wrx_champ free_roam clubs`. A pausa, o `service_area` e as listas `SBScrollableItemFlow` têm um id por linha.

A conversão para XML legível (`/tmp/ui/screens.xml`) mostra esses ids numa linha só. Isso é artefato da conversão. A frase de [ui_data.md](ui_data.md) que diz que `SBScrollableItemFlow` tem "os ids numa linha só" vem desse artefato.

`[id]` numa célula repete o vizinho e estende a célula, como `events garage` / `[events] staff`. PROVÁVEL: é uma célula que ocupa duas posições da grade.

## Behaviours por função

86 tags de behaviour aparecem nos dados. O registro de classes é `lea rdx,[nome]; call 0x1401519b0` e tem 931 nomes: behaviours, estados, tasks, DisplayHelpers e GroupElements. Exemplos: `SBItemStack` em `0x140023565` (nome em `0x141236a90`), `IBStackItem` em `0x14001d515` (`0x14122e310`), `SBHotButtonScreenEvent` em `0x1400234e5` (`0x141236aa0`).

### Navegação e listas

| Behaviour | Função | Atributos importantes | Depende de C++? |
| :--- | :--- | :--- | :--- |
| `SBGridItemFlow` | Grade de navegação | `wrapH`, `wrapV`, `allow_duplicate_nodes`, `active_default`, `default_on_back`, `source_of_default` | Não |
| `SBScrollableItemFlow` | Lista com rolagem | os de cima + `scroller_glyph`, `scroll_offset`, `smooth_scroll`, `scroll_axis` (`horizontal`/`vertical`) | Não |
| `SBItemStack` + `IBStackItem` | Empilha itens visíveis, esconde os ocultos | `glyph` | Não |
| `IBGroupPositioner` | Agrupa e pula | `prevent_select` (`0x140d49f59`), `quick_navigate_step` | Não |
| `SBDynamicItemFlow`, `SBSelectableDynamicItemFlow`, `SBDynamicItemFlowWithApply` | Lista virtualizada | `top_index`, `selected_index`, `view_size`, `list_size`, `view[%u]`, `View item_prefix`, `Scroller auto_repeat long_list` | Sim, a lista vem do C++ |
| `SBTabGroup`, `SBScreenTabControl` | Abas | `tabs.info` / `tab screen_id`, `max_visible_tabs`, `hide_single_tab` | `SBTabGroup` sim |
| `SBManualScroll`, `BAutoScroll` | Texto rolável | — | Não |

Os atributos de fluxo estão no trecho `0x140d23200`–`0x140d24100`: `wrapH`/`wrapV` (`0x140d23364`/`0x140d2339b`), `allow_duplicate_nodes` (`0x140d233d9`), `default_on_back` (`0x140d23afa`), `scroller_glyph` (`0x140d23eb8`) e `smooth_scroll` (`0x140d23f46`). Atributos de `Item`: `glyph`, `enable_cursor` (`0x140d4bc2f`), `cursor_hit_glyph`.

Estilos de scroller em `scroller_styles.xml`: `default` (`fixedSizeStep 0.48`), `legalscroll`, `credits`, `popup`, `notched` e outros. As repetições de tecla ficam em `auto_repeat_data.xml`.

### Valores e seleção

| Behaviour | Função | Atributos |
| :--- | :--- | :--- |
| `IBComboTextStatic` | Combo com lista fixa | `value_list`, `show_steps` (`0x140d4914b`), `explicit_string` (`0x140d491e7`) |
| `IBComboTextData` | Combo com lista do modelo | `text_glyph`, `list_value`, `watch_data_list` |
| Base dos combos (`0x140d48200`–`0x140d49400`) | — | `arrows_enabled` (`0x140d4838d`), `wrap`, `disable_on_one_option` (`0x140d4843c`), `sfx`/`ss_toggle`, filho `Scroller` |
| `IBSelectableSimple` | Gera um evento ao selecionar | `select_value` (`0x140d49fbb`, `0x140d4aca0`), `out` (sempre `event`), `cursor_only`, `release_event`, `help_text` |
| `IBTextEntry` | Teclado virtual | `max_chars`, `allow_empty`, `default_value`, `empty_value_string`, tipo de teclado |

`IBSelectableSimple` tem 162 valores distintos de `select_value` nos dados. O evento é gravado em `event` e o C++ do estado dono o consome.

Tipos de teclado (trecho `0x140d4b200`): `numeric`, `full`, `full_no_spaces`, `gamertag`, `password`, `latin`, `latin_no_spaces`, `email`. Valores de `max_chars` usados: 16, 24, 47 e 127.

### Texto e visibilidade

| Behaviour | Função |
| :--- | :--- |
| `BTextStatic` | Texto fixo (vtable `0x1413d2e58`, aplica com `0x140d040c0`) |
| `BTextData` e afins | Texto ligado ao modelo, com `format_id` |
| `BVisibilityControlStatic` / `BVisibilityControlData` | Mostra ou esconde (predicado `0x140d09380`) |
| `BSwitchStatic` | Escolhe o objeto do item |

`value_scale` (`0x140d2062a`) e `uppercase` (`0x140d20832`) estão no trecho dos display helpers.

### Descrição, popups e progresso

- **Painel da direita** (`smart_contextual_info.text_title` / `.text`). Na maioria das telas é `BTextStatic` + `BVisibilityControlStatic`, fixo por tela. É dinâmico em `input_bindings` e `profile_save_management` (`sidebar.title` / `sidebar.description`) e nas telas de tuning (`option_description` via `IBItemFlowIndex index_data_path="selected_option_index"` + `SBTuningOptionTextures`). CONFIRMADO: ajuda por item exige alguém escrevendo no modelo de dados, seja o C++ ou a DLL.
- **Diálogos**: `SBDialogBox` lê `options[%u].value`, `.enabled`, `.id`, `title`, `body`, `layout` (`default`/`scrolling`), `image`, `spinner` e `back_id`.
- **Progresso**: `IBFractionAnimated` (`anim_id` como `research_bar`, `slider_fraction`, `progress_bar`) e `SBRepairsBar`.
- **Espera**: `SBLoadSaveSpinner` na camada `foreground`.
- **Resultados**: `SBLabelledResultsGroups`.

### Registrados e sem uso nos dados

Estes 34 behaviours existem no executável e nenhuma tela os usa. São candidatos para mods, sem garantia de funcionar fora do contexto original:

`BColourTint`, `BContentSpinner`, `BEnvironmentImage`, `BResultRow`, `BStarRating`, `BSurfaceType`, `BTextureStreamed`, `BTodaysDate`, `BTuningGearGraph`, `IBAnimatedConditional`, `IBCameraTarget`, `IBChoiceBase`, `IBDataConditionEnabled`, `IBDataValueBoolSet`, `IBDataValueIntSet`, `IBDataValueStringSet`, `IBHackChangeActiveOnDisable`, `IBIconMeter`, `IBIntDataEnabled`, `IBModifierFractionAnimated`, `IBSelectable`, `IBSelectableTriggerAnim`, `IBSliderColourGradient`, `IBTargetRowAnims`, `SBAnimMagnet`, `SBChampSlotList`, `SBDisableOnIdle`, `SBHandlingModel`, `SBOpenScreenDelayedAnim`, `SBRepairsBarAnim`, `SBTargetAnimations`, `SBVehicleClassSelectStatPanelControl`, `SBVehicleSelectAnimationControl`, `SBVehicleSelectStatPanelControl`.

Os mais úteis são `IBDataValueBoolSet`, `IBDataValueIntSet` e `IBDataValueStringSet`. Pelo nome, eles gravariam um valor no modelo de dados ao selecionar, sem C++ (HIPÓTESE). O parser também conhece identificadores que nenhum dado usa, como `instant_animation`, `disable_audio`, `allow_open_invert`, `focus_gained`, `focus_lost` e `enable_behaviour`.

## Hot buttons e actions

Nomes de action no executável (strings contíguas de `Down` em `0x1413d23c4` a `CancelKeybind` em `0x1413d24d8`, CONFIRMADO): `Down`, `Select`, `Button3`, `Button4`, `StartButton`, `SelectButton`, `ViewTweakLeft`, `ViewTweakRight`, `ViewTweakUp`, `ViewTweakDown`, `ViewTweakIn`, `ViewTweakOut`, `LeftShoulder`, `RightShoulder` (`0x1413d2478`), `LeftTrigger` (`0x1413d2488`), `RightTrigger`, `ScrollDown`, `ScrollUp`, `CursorButton`, `CancelKeybind` (`0x1413d24d8`). Os dados também usam `Back`, `Left` e `Right`.

### `SBHotButtonScreenEvent`

Botão de tela que emite um evento. Parser em `0x1401afa00`–`0x1401b0100`.

- Atributos: `action`, `action_secondary`, `event_primary` (`0x1401afe7f`), `event_secondary`, `help_text`, `help_text_explicit`, `watch_help_text_data`, `enable`, `watch_enable_data`, `enable_data_inverted`, `enable_click`, `primary_audio`, `secondary_audio`, `bottom_button_item`, `show_disabled_items`.
- Uso de `action` nos dados: `Back` 186, `Select` 59, `Button3` 34, `Button4` 17, `RightShoulder` 1, `LeftShoulder` 1.
- Eventos comuns: `back`, `next`, `close_context_menu`, `restore_defaults`, `toggle_filter`.

`help_text_explicit` e `event_secondary` não aparecem em nenhum dado. PROVÁVEL: o primeiro mostra uma legenda literal na barra de botões, sem chave `lng_`.

### Outros

| Behaviour | Actions usadas |
| :--- | :--- |
| `IBHotButton` | `Button3`, `Button4`, `Select`, `Left`, `Right`, `CancelKeybind` (`listen_without_focus`) |
| `IBHotButtonDataGated` | `LeftShoulder`/`RightShoulder` (`quick_nav`), `Button3` |
| `IBContextMenu` | `open_action` = `Button4` ou `Button3` |
| `SBContextMenu` | `Button4`, `Button3`, `Select` |
| `SBTabGroup` | `LeftTrigger`, `RightTrigger` |

Oportunidade: `ScrollUp`/`ScrollDown`, `StartButton` e `ViewTweak*` existem como action e nenhuma tela os usa num hot button (HIPÓTESE que funcionem ali).

## Texto

### Quebra e encaixe

O enum de quebra é lido em `0x140d24c4a`–`0x140d24cd8` e gravado em `estilo+0x58` (CONFIRMADO). Strings em `0x1413d4a40`–`0x1413d4a90`; `none` em `0x1411f624c`.

| Valor | Nome | Efeito |
| :--- | :--- | :--- |
| 0 | `none` | Sem ajuste |
| 1 | `wordwrap` | Quebra em palavras |
| 2 | `clampwidth` | Limita a largura (PROVÁVEL: encolhe só na horizontal) |
| 3 | `clampuniform` | Encolhe mantendo a proporção (PROVÁVEL) |
| 4 | `truncate` | Corta |

- Os 74 estilos de `text_styles.xml` usam `wrapping clampuniform`.
- Valores de `wr` nos nós de texto das cenas: `-1` em 444, `3` em 131, `2` em 78, `1` em 29, `0` em 12, `4` em 4. PROVÁVEL: `-1` herda o estilo, ou seja, `clampuniform`.
- A string `ellipsis` (`0x14144efa1`) não tem referência no código. CONFIRMADO: não há reticências.

### Larguras por componente

| Componente e glyph | Estilo | `wi` | `wr` |
| :--- | :--- | :--- | :--- |
| `smart_hub_button_5col` `text_title` | `_34_din_bold` | 3.45 | 3 |
| `toggle_15col` `text_title` | — | 3.32 | -1 |
| `toggle_15col` `text_value` | — | 6.0 | -1 |
| `nub_slider` `text_value` | — | 1.48 | — |
| `label_15col` | — | 9.45 | 4 (`truncate`) |
| `screen_header_text` | `_70_din_ita` | 9.7 | 3 |
| `smart_contextual_info.text` | `_22_roboto_cnd` | 5.5 | 1 (`wordwrap`) |
| `smart_contextual_info.text_title` | `_44_din_ita` | 4.82 | -1 |
| `popup_scroll` `message` (TEXTDOC) | — | 12.38 | 1 |
| `smart_button` `text_title` | — | 20 | — |

Consequência: um rótulo longo no `smart_hub` ou num toggle encolhe em vez de quebrar (PROVÁVEL). O `label_15col` corta. A descrição da direita quebra linha.

### Tamanhos observados

- `bra.lng` tem 11.373 chaves `lng_`, 643 delas com `_caps`. `lng_pause_menu_title_caps` não existe.
- Maior valor: 30.018 caracteres (EULA, dentro de um scroller). 564 valores passam de 200 caracteres e 867 têm `\n`.
- A descrição de "Gráficos avançados" tem 178 caracteres.

### Maiúsculas

CONFIRMADO: `localise_upper` não converte em tempo de execução. O formatador em `0x140d2a4d0` monta `chave + "_caps"` com `sprintf "%s%s"` (`0x1411f74c8`) e a string `_caps` (`0x1413d79e8`, referência em `0x140d2a575`), e depois procura a chave no idioma (`0x1403a8820`). Um mod que use `localise_upper` precisa fornecer a variante `_caps`. O atributo `uppercase` (`0x140d20832`) existe no trecho dos display helpers, e seu efeito não foi medido.

### `format_id`

Definidos em `value_display_helpers.xml`:

| Helper | `format_id` |
| :--- | :--- |
| String | `explicit`, `localise`, `localise_upper` |
| Int (31) | `default`, `double_digits`, `credits`, `credits_or_free`, `currency`, `event_count`, `champ_count`, `event_count_caps`, `vehicle_count`, `reputation`, `points`, `points_or_null`, `percentage_or_dash`, `percentage`, `percentage_diff`, `bhp`, `rallyx_heat`, `kgfm_visc_diff`, `angle_degrees`, `angle_radians`, `plus_or_minus`, `protour_points`, `protour_division`, `protour_tier`, `stage_count`, `race_count`, `tuning_plus_or_minus`, `contract_duration`, `mail_expiry_events`, `mail_expiry_champs`, `joyride_blocks` |
| Double | `default`, `gamma`, `gear_ratio`, `angle_degrees`, `angle_radians`, `percentage` |
| Distance | `long_distance`, `short_distance`, `short_distance_no_decimal`, `tiny_distance` |
| Speed | `default`, `small_speed`, `large_speed` |
| Torque | `default`, `small_torque` |
| Weight | `vehicle_weight` |
| Time | `racetime`, `racetime_or_null`, `long_racetime_or_null`, `racetime_diff`, `repairs_cost`, `playtime`, `remaining_time`, `remaining_day_time` |
| Ordinal | `ordinal`, `ordinal_upper` |
| Outros | Area (3), SurfaceTension (2), Timestamp (12), Grade (`default`) |

Uso nos dados: `explicit` 2722, `localise` 1612, `double_digits` 685.

## Estados genéricos e popups

`states.xml` tem 494 tags. O executável registra 599 nomes `State*`/`Task*`.

### Genéricos, reutilizáveis por dados

| Estado ou task | Parâmetros | Uso vanilla | Grau |
| :--- | :--- | :--- | :--- |
| `StateScreenFECore` (registro `0x1400399ac`) | `screen_name` | 6 | CONFIRMADO no jogo ([ui_data.md](ui_data.md)) |
| `StateScreen` (`0x14003922c`) + `TaskUIScreen` | `screen_name`, `force_wait_screen_close` | créditos | CONFIRMADO nos dados |
| `GameState` (`0x14002e0bc`) | só contém tasks | 71 | CONFIRMADO nos dados |
| `StateGlobal` + `TaskUIDialog` | `screen_name="dialog"` | — | CONFIRMADO nos dados |
| `TaskDisplayDialog` | `id=<mensagem>` | 56 estados, 1.014 nós de fluxo | CONFIRMADO nos dados |
| `TaskTimeout` | `duration`, `link`, `block_transitions`, `online_only` | — | CONFIRMADO nos dados |

Registrados e sem uso nos dados: `StateWaitForSignal` (`0x14004352c`), `StateScreenTestImgui` (`0x14002ac7c`), `StateShowWaitingSpinner`, `StateShowSavingSpinner`, `StateScreenDataTabbed`, e as tasks `TaskOpenBrowser`, `TaskDoTransition`, `TaskGenerateSpline` e `TaskTutorialSetViewed`.

### Dependentes de C++

Telas que leem listas ou eventos preenchidos por um estado específico: fluxos dinâmicos, `SBTabGroup` com `tabs.info`, `SBTuningOptionTextures`, gráficos básicos e avançados (a lógica de aplicar está no C++), e qualquer tela cujos `select_value` só um estado dono entende.

### Diálogos (`message_dialogs`)

`frontend/message_dialogs/flow.xml`, `network.xml` e `save.xml` são BXML e têm 415 mensagens (249 em `flow`).

- Tipos: `global_default` 375, `global_default_data_store` 40.
- Número de opções: 0 em 17, 1 em 208, 2 em 175, 3 em 14, 5 em 1. O máximo vanilla é 5.
- Chaves: `title`, `body`, `token` (string ou int32 com `ds_address`), `default`, `back`, `priority`, `layout`, `user_data`.
- Exemplos prontos: `are_you_sure_confirmation` (sim/não) e `exit_game_confirmation`.

Modelo, visto na pausa: o nó `231066979` tem 7 filhos de diálogo. O nó `162204671` é um `GameState` com `TaskDisplayDialog id="service_area_shakedown_confirmation"` e links `yes`, `no` → `231066979` e `closed` → `231066979`. Os ids das opções viram ids de link do fluxo, e `closed` cobre o cancelamento.

Consequência: um sim/não reaproveitando uma mensagem existente é só dado (CONFIRMADO pelo padrão vanilla). Uma mensagem nova exige patch de `message_dialogs`. Segundo [ui_data.md](ui_data.md), esses arquivos passam pelo mesmo parser. Substituir por XML texto é HIPÓTESE.

## Perguntas em aberto e como testar

| # | Pergunta | Teste no jogo |
| :--- | :--- | :--- |
| 1 | `smart_set.11+` funciona no `smart_hub`? | Patch da pausa com 14 itens (`item_0` a `item_13`, um id por linha). Ver se aparecem e se o cursor chega ao último. |
| 2 | Quantas linhas cabem no `smart_hub` antes de sair da tela? | O mesmo patch com 16 e 20 itens. Anotar onde o último item deixa de ser visível. |
| 3 | O `smart_screen` rola além de 29 itens? | Clonar `advanced_graphics` com 40 `toggle_15col`. Descer até o fim. |
| 4 | `message_dialogs` aceita XML texto no lugar de BXML? | Trocar só `body` de `are_you_sure_confirmation` por texto em XML plano e abrir o diálogo. |
| 5 | `GameState` + `TaskUIScreen` abre uma tela qualquer? | Estado novo com `TaskUIScreen screen_name="<clone>"` e link de volta. Comparar com `StateScreenFECore`. |
| 6 | `IBDataValueBoolSet`/`IntSet`/`StringSet` gravam no modelo? | Item com o behaviour e um `BVisibilityControlData` observando o mesmo caminho. Ver se o outro item aparece ao selecionar. |
| 7 | `explicit_string`, `arrows_enabled` e `wrap` nos combos | `toggle_15col` com `IBComboTextStatic` e cada atributo ligado e desligado. |
| 8 | `wr=-1` herda `clampuniform`? | Rótulo de 60 caracteres num `toggle_15col`. Encolher confirma, quebrar ou cortar refuta. |
| 9 | Rótulo longo no `smart_hub` encolhe? | Item da pausa com 50 caracteres via `explicit`. |
| 10 | `help_text_explicit` mostra legenda literal? | `SBHotButtonScreenEvent action="Button3" help_text_explicit="Teste"` numa tela clonada. |
| 11 | Como o stacker cria filhos acima dos nós da cena? | Leitura (somente leitura) de `/proc/<pid>/mem` com a pausa aberta, contando os filhos do stacker `smart_set` antes e depois de mostrar o item 10. |
| 12 | Actions sem uso (`ScrollUp`, `StartButton`, `ViewTweak*`) funcionam num hot button? | `SBHotButtonScreenEvent` com cada action e um `event_primary` registrado pela DLL. |
| 13 | `[id]` numa grade é célula estendida? | Grade 2×2 com `a b` / `[a] c`. Ver se descer de `a` vai para `c` ou para `a`. |
