# Renderização da UI

Como o frontend e o HUD da corrida desenham as coisas, e por onde um mod pode desenhar de forma nativa. Investigação de 2026-10-05, só leitura: dados extraídos com `tools/egodata` e desmontagem estática do `dirtrally2.exe` (base `0x140000000`). Nada foi escrito no jogo nem no processo.

Marcadores: **CONFIRMADO** (visto nos dados ou no código), **PROVÁVEL** (indício forte, sem teste), **HIPÓTESE** (a testar).

## Resumo

- Não há CSS, HTML, Scaleform nem Flash. O único `.css` da instalação é `readme/readme.css`, que estiliza os `readme_*.html`; o motor não o lê. CONFIRMADO.
- A UI é uma **cena 3D com câmera ortográfica**, igual à de um modelo: nós com `TRANSFORM`, malhas (`RENDERNODE` + `RENDERSTREAMINSTANCE`), materiais (`SHADERINSTANCE`) e texturas, tudo em PSSG. CONFIRMADO.
- Quem faz o papel de "folha de estilo" são três XMLs binários (BXML) em `frontend/configs/`: `text_styles.xml`, `global_parameter_styles.xml` e `scroller_styles.xml`. CONFIRMADO.
- Cor e opacidade não ficam no estilo de texto: são os parâmetros `DiffuseColour` e `Alpha` do material da malha, e as animações mexem neles pelo nome. CONFIRMADO.
- Os estados visuais (destaque, desabilitado, hover) são uma **timeline**: o C++ manda um evento com nome (`activate`, `deactivate`, …) e a timeline toca até um quadro. Parece Flash, mas é PSSG. CONFIRMADO.
- Para desenhar fora das telas existentes há um **blitter 2D** no motor (quads com e sem textura, com base de dados e câmera próprias). Ver [Caminhos para desenhar](#caminhos-para-desenhar-de-forma-nativa). PROVÁVEL que sirva; não testado.

## Arquivos

| Arquivo | Pacote | Conteúdo |
| :--- | :--- | :--- |
| `frontend/databases/persistentDB.pssg` | `game_1.dat` | Cenas das telas e componentes do frontend (`fe/screens/*`, `fe/component/*`) |
| `frontend/databases/d_osd.pssg`, `d_osd_ingame.pssg` | `game_1.dat` | HUD da corrida: 95 raízes `osd/component/*` e `osd/unit/*` |
| `frontend/databases/hotReload.pssg` | `game.nefs` | Só os eventos de um botão e um bloco vazio; parece sobra de ferramenta |
| `frontend/bundles/b_*.pssg`, `frontend/fonts/*.pssg` | ambos | Texturas, fontes (SDF/MSDF) e imagens de veículos e locais |
| `frontend/configs/text_styles.xml` | `game_1.dat` | 74 estilos de texto |
| `frontend/configs/global_parameter_styles.xml` | `game_1.dat` | 38 fades de opacidade (`W0`) com duração nomeada |
| `frontend/configs/scroller_styles.xml` | `game.nefs` | 7 estilos de rolagem |
| `frontend/configs/durations.xml` | `game_1.dat` | Durações nomeadas usadas pelos fades e eventos (`default`, `tab_open`, …) |
| `frontend/configs/fe_shadersetup.xml` | `game.nefs` | Quais parâmetros de cada shader a animação pode mexer (texto puro) |
| `frontend/fonts/font_list_def.xml` | `game.nefs` | Lista de fontes |

O carregador das três configs de estilo despacha pelo nome do documento em `0x140d33246`: `global_parameter_styles` → `0x140d26050`, `text_styles` → `0x140d26ca0`, `scroller_styles` → `0x140d26b50`. O mesmo despacho trata `camera_vibrations`, `noise_tweaks`, `spring_tweaks` e `handy_cam_settings`. CONFIRMADO.

## BXML

Formato diferente do XML binário do `screens.bin` ([UI Data](ui_data.md)). Decodificado em 2026-10-05; a releitura dos três arquivos de estilo bate com a estrutura esperada. CONFIRMADO.

- Magia `\0BXML` (5 bytes). O log do `UiData` mostra a magia como `0x4d584200`; há uma variante `0x4d584201` (`\1BXM…`, ex.: `value_display_helpers.xml`, `message_dialogs/*.xml`) que este decodificador **não** lê.
- Elemento: `u8 0`, `u32 tamanho` (LE), `u8 nº de atributos`, nome terminado em zero, pares `chave\0valor\0`.
- Depois dos atributos vêm os filhos (cada um começa com `0`) e, opcionalmente, o texto terminado em zero.
- Fim do elemento: `u8 0` (texto vazio ou terminador do texto) seguido de `u8 4`, `u32 tamanho`. O registro de fim tem `tamanho` bytes a partir do `4`.
- Sobram 12 bytes no fim do arquivo, não interpretados.

## Estilos

### `text_styles.xml`

Cada `<style>` tem: `id`, `font`, `material`, `fontType`, `height`, `depth`, `halign`, `wrapping`, `leading`, `tracking`, `boldness`, `softness`, `line_indent`, `textscale`. Exemplo:

```xml
<style id="_34_din_bold" font="din_cnd_bold" material="NONE" fontType="texture" height="34"
       depth="1" halign="left" wrapping="clampuniform" leading="0" tracking="0"
       boldness="0.5" softness="0" line_indent="0" textscale="0.01"/>
```

- Fontes: `din_cnd_bold`, `din_cnd_bold_ita`, `roboto_cnd_reg` (e as variantes `_jpn`).
- O nome segue `_<altura>_<fonte>[_tr_<tracking>]`: `_22_roboto_cnd`, `_44_din_ita`, `_70_din_ita_tr_300`.
- `height` está em pixels de 1080p; `textscale 0.01` converte para unidades da cena (1 unidade = 100 px). Bate com a câmera.
- **Não há cor.** A cor do texto é o `DiffuseColour` do material do nó de texto.
- O nó de texto da cena escolhe o estilo pelo atributo `sn` e pode sobrescrever largura (`wi`), alinhamento (`ha`) e quebra (`wr`). Ver [UI Limits](ui_limits.md#texto). No `UINODETEXTDOC` o próprio texto troca de estilo com `{s:<id>}` ([UI Tabs](ui_tabs.md#texto-rico-no-painel-resolvido-2026-10-02)).

### `global_parameter_styles.xml`

Fades de opacidade da camada: `<style name="tab_fade_in" duration="tab_open" finalW0="1"/>`. `initialW0`/`finalW0` vão de 0 a 1 e `duration` é um nome de `durations.xml`. Exemplos: `defaultfadein`, `mb_fade_in`, `osd_in`, `osd_out`, `tooltip_fade_in`, `tile_fade_out` (para `0.2`).

### `scroller_styles.xml`

Física da rolagem: `type` é `fixedSizeStep`, `free` ou `notched`, com `acceleration`, `maxVelocity`, `sizeStep`, `threshold` (ou `gradient` e `offset` no `notched`). Estilos: `default`, `legalscroll`, `whats_this`, `credits`, `popup`, `default_notched`, `large_carousel_scroll`.

## A cena

### Câmera e unidades

As quatro câmeras do frontend (`fe/main_scene_root`, `fe/background_scene_root`, `fe/foreground_scene_root`, `fe/subtitles_overlay_root`) são ortográficas, `left -9.6`, `right 9.6`, `top 5.4`, `bottom -5.4`. A tela tem 19,2 × 10,8 unidades com a origem no centro: **1 unidade = 100 px em 1920×1080**, `y` para cima. CONFIRMADO.

### Tipos de nó

Contagem em `persistentDB.pssg`:

| Nó | Qtd. | Papel |
| :--- | ---: | :--- |
| `ROOTNODE` | 674 | Raiz de cada tela ou componente (`<caminho>_root`, apelido = caminho) |
| `NODE` / `RENDERNODE` | 2526 / 1723 | Grupo / grupo com malha; o `nickname` é o nome usado nos glyphs (`text_title`, `highlight`) |
| `TRANSFORM` | 4927 | Matriz 4×4 em float big-endian; translação em `[12..14]` |
| `RENDERSTREAMINSTANCE` | 1711 | Malha; o atributo `shader` aponta o material |
| `SHADERINSTANCE` | 1564 | Material: grupo de shader + valores dos parâmetros |
| `TEXTURE` | 295 | Texturas (atlas, padrões) |
| `USERDATA` | 7840 | Liga o nó aos objetos de UI (`#etex…`, `#elay…`, `#AnimSet!…`) |
| `UINODE*` / `UIMOD*` | — | Comportamento de UI; ver abaixo |
| `FEEVENT*`, `FEANIMDATA` | ~5300 | Timeline de eventos |
| `NeAnimSet`/`NeAnimClip`/`NeAnimPacket_*` | ~1300 | Curvas de animação |

`UINODE*` e `UIMOD*` vistos nos dados: `TEXT`, `TEXTDOC`, `ANIMATED` (`xr` = inclui outro componente), `STACKER`, `SCROLL`, `SWITCH`, `SCREEN`, `NULL`, `POINT`, `CENTRE`, `SCALER`, `BLUR`, `CAMERA`, `TARGET`; `MODLAYER` (`la` = ordem de desenho), `MODBOUNDLESS`, `MODUNIQUE`, `MODSCALEE`, `MODSTRETCH`, `MODSCISSOR`. O executável registra também `UINODECLIP`, `UINODEXREF`, `UINODERELPOS`, `UINODECONNECTOR`, `UINODEMAGNETMOVER`, `UINODEMAGNETSCALER`, `UINODERRO` e `UIMODVOLATILE`, sem uso nesta base.

Registro dos tipos: `0x1408c5c30(descritor, nome, fábrica, pai)`. `UINODETEXT` tem descritor `0x142017490` e fábrica `0x140d1df10`; `UINODETEXTDOC` tem descritor `0x142017570`, fábrica `0x140d1dfd0` e **pai `0x142017490`**, ou seja, herda de `UINODETEXT`. CONFIRMADO.

### Exemplo: botão da pausa

`fe/component/smart_hub/smart_hub_button_5col_root`, o molde dos itens do menu de pausa:

```
ROOTNODE smart_hub_button_5col
  USERDATA #fenc!207, #AnimSet!118            contagem de nós, animações
  RENDERNODE text_title   T=(0.10,-0.11)       layer 50, UINODETEXT etex3021
    material smart_pill_text!69                ui_text_msdf_shadow.fx
  NODE null               T=(0.038,0.322)
    RENDERNODE highlight  T=(-0.038,-0.322)    layer 30, boundless
      material smart_pill_highlight!42         ui_2d_pattern_stretch.fx + dirt_texture_pill_tiled.tga
    NODE point_0 / point_1                     cantos (-0.038,-0.022) e (3.382,-0.622)
  RENDERNODE divider      T=(0,-0.30)
    material 02 - Default!12                   ui_2d_colour.fx, branco, alpha 0.1
```

`etex3021` é `UINODETEXT sn=_34_din_bold wi=3.45 ha=3 wr=3`. Os pontos `point_0`/`point_1` marcam o retângulo do item (3,42 × 0,60 unidades, 342 × 60 px).

### Materiais

15 grupos de shader na base. Os mais usados: `ui_text_msdf_shadow.fx` (615), `ui_2d_image.fx` (404), `ui_2d_colour.fx` (360), `ui_2d_pattern_stretch.fx` (103). Todos têm `DiffuseColour` (float3) e `Alpha`. Os de texto têm também `Bold`, `ShadowColour`, `ShadowAlpha`, `ShadowBold`, `ShadowSoftness`, `ShadowOffsetX/Y` e o mapa de distância (`TDistanceMap`). CONFIRMADO.

O `parameterID` de cada `SHADERINPUT` é o índice na lista de `SHADERINPUTDEFINITION` do grupo. No `smart_pill_text`, o índice 8 é `DiffuseColour = (1,1,1)` e o 9 é `Alpha = 0`: o texto nasce invisível e a animação o acende. CONFIRMADO.

`fe_shadersetup.xml` diz quais parâmetros a animação alcança por shader. O padrão (`ParameterRetrieval_float3float id="default"`) é `DiffuseColour` + `Alpha`; shaders específicos ganham um segundo canal, por exemplo `ui_2d_image_adjust` → contraste, brilho, saturação e tinta.

### Animação e estados

Cada componente tem um `FEANIMDATA` (quadro padrão em `defaultFrame`) e uma cadeia de `FEEVENT` com apelido. No botão da pausa:

| Evento | Ação |
| :--- | :--- |
| `activate` | Toca até o quadro 1,0 (destacado) |
| `deactivate` | Toca até 0,667 (normal; é o `defaultFrame`) |
| `active_hover` | Até 2,0 |
| `esport_activate` | Até 2,333 |
| `disable`, `active_disable` | `FEEVENTNEAREST` + sequência de `PLAYTO`/`GOTO` |
| `_playtofraction` | `FEEVENTPLAYTOFRAC` entre 0 e 3,333 |

Os pacotes de animação nomeiam o alvo como `matinst=<material>#<parâmetro>`: `matinst=smart_pill_highlight!42#Alpha`, `matinst=smart_pill_text!69#Alpha`, `matinst=smart_pill_text!69#DiffuseColour`. O destaque é só isso: a pílula ganha alpha e o texto muda de cor. CONFIRMADO nos bytes.

Os behaviours do `screens.bin` disparam esses eventos pelo nome; o nome não aparece no `screens.bin`, então o vínculo está no C++ dos behaviours. PROVÁVEL.

### Formato das curvas (`NeAnimPacketData_B1`/`_B4`)

Decodificado em 2026-10-05. CONFIRMADO: os 965 canais de `persistentDB.pssg` e `d_osd.pssg` decodificam, e todos os alvos batem com um `SHADERINSTANCE`.

Big-endian. Cabeçalho do pacote: `u32 nº de canais`, `u32` (sempre `0x10`). Cada canal:

| Campo | Conteúdo |
| :--- | :--- |
| `u32 n` + `n` bytes | Alvo `<id do material>#<parâmetro>`, ex.: `matinst=smart_pill_text!69#Alpha` |
| `u32 9` + `parameter` | Tipo do canal. Só existe `parameter`: as curvas animam parâmetros de material, nunca transformações |
| `u32 k` | Nº de chaves, contando a final |
| `u32`, `u32` | Não usados na leitura |
| `k-1` chaves | `f32 tempo`, `4·W` coeficientes de tempo, `4·W` coeficientes de valor |
| chave final | `f32 tempo`, `4·W` coeficientes de tempo, `W` valores finais |

`W` é 1 no `B1` e 4 no `B4` (cores). Os coeficientes ficam em estrutura de arrays: `v0[W]`, `v1[W]`, `v2[W]`, `v3[W]`. O valor no segmento é `v0 + v1·s + v2·s² + v3·s³`, com `s` de 0 a 1 entre o tempo da chave e o da seguinte; em `s = 1` dá o valor inicial da próxima chave. Os coeficientes de tempo (sempre perto de `-0,5`, `-2500`, `-1667`, `0`) parecem corrigir a velocidade dentro do segmento. Ignorá-los não muda os valores nas pontas, que são os quadros dos eventos. Exemplo, alfa do texto do botão da pausa: 0 → 0,3 (quadro 0,333) → 1,0 (0,667, repouso) → 1,0 (1,0) → 0,3 (1,333). A cor vai de branco para `(0,022; 0,022; 0,030)` em 1,0: texto escuro na pílula destacada.

Os parâmetros animados: `Alpha` (600 canais), `DiffuseColour` (264), `interpUV` (27), `PatternStrength` (18), `Ramp` (14), sombra do texto, `MinEdge`/`MaxEdge` e as cores do degradê.

Cadeia na cena: `ROOTNODE` → `USERDATA #AnimSet!…` → `NeAnimSet` → `NeAnimSetClipRef` → `NeAnimClip` → `USERDATA` (`FEANIMDATA`, com `defaultFrame` e o primeiro `FEEVENT`) + `NeAnimClipPacketRef`. O quadro de um evento é o do último `PLAYTO`/`GOTO` da sequência dele. Eventos por quantidade de raízes: `activate`/`deactivate` (416), `disable`, `active_hover`, `esport_activate` (413), `open` (138), `close` (129), `open_back`, `close_back`.

### Nós de transição

As posições de entrada e saída das telas não estão nas curvas. Elas ficam em nós `NULL` filhos da raiz com nome de transição (`null_trans_main`, `null_trans_secondary`, `null_trans_title`, `anim_node`, `anim_main`, `anim_secondary`, `anim_tabs`, `anim_transition`), deslocados só em x (−2 ou −5) e com y = 0. Prova: `main_menu`, sem nó de transição, põe o conteúdo em x = 1,24; `smart_screen` põe os mesmos 1,24 dentro de um `null_trans_*` em x = −2, ou seja, deslizando da esquerda. A posição final é translação 0. PROVÁVEL; o mecanismo que anima esses nós não foi achado.

## Composição de uma tela

CONFIRMADO nos dados e conferido no visualizador:

1. **Hospedagem.** `fe/main_scene` → `centre_safeframe` → `screen_directory` (xr `fe/screen_list/screen_directory`, em **(−9,6; 5,4)**, o canto superior esquerdo) → `screen_trans_scaler` → `screen_choice` (switch). As cenas de tela usam origem no canto superior esquerdo e y negativo para baixo. Alguns filhos de `screen_choice` deslocam a tela (ex.: `credits`, `replay` e `rewards_flow_intro` em (9,6; −5,4)).
2. **Objeto.** O `object` de uma `Screen` é o nome de um **filho do switch** apontado pelo `glyph` da tela, e não o nome de uma cena. Telas de topo: glyph `screen_directory.screen_choice`; o filho com o nome do objeto tem o xr da cena (ex.: `loading_screen` → `fe/screens/loading/map_stats_loading`, `repairs` → `repairs_screen`). Páginas embutidas: o glyph aponta um switch dentro da tela-mãe (`data_parent_override` ou a tela cuja cena tem o caminho), ex.: `sa_stage_info` = glyph `service_area_infobar.switch`, objeto `stage_info`, dentro de `service_area`.
3. **Glyph de item.** Caminho de apelidos separados por ponto. Cada segmento é o descendente mais próximo com esse apelido, atravessando os xr (`UINODEANIMATED`/`UINODESCREEN`) e só o filho escolhido de cada switch. Índices além dos declarados (`smart_set.9` num molde de 0 a 7) são cópias do último, com o passo dos anteriores.
4. **Switch.** `BSwitchStatic object=` escolhe o filho; sem escolha vale o `ir`. Os glyphs do item que vêm depois (`text_title`) são procurados no filho escolhido.
5. **Lista.** Posições numéricas sem item ficam vazias; o `UINODESTACKER` fecha os buracos. Dentro de um `UINODESCROLL`, só as posições da cena aparecem sem rolar (11 na `smart_screen`).
6. **Texto.** A posição do nó de texto é a **linha de base**. `ha`: 0 = do estilo (esquerda), 1 = direita, 2 = centro, 3 = esquerda (deduzido dos nomes dos nós; PROVÁVEL). `wi` é a largura em unidades e `wr = -1` herda a quebra do estilo.
7. **Estado.** Os valores estáticos dos materiais são o quadro 0 da animação, não o que se vê. O estado visível é a curva avaliada no fim do `open` (telas e painéis) ou no `defaultFrame` (botões, que é o `deactivate`); o item em foco usa o fim do `activate`.
8. **Shaders.** `ui_2d_image_screenblend` é mistura "screen": preto não altera nada. `UIMODSTRETCH` estica a malha até cobrir a tela.

Incoerências dos próprios dados, que o jogo ignora: glyphs de visibilidade que não existem no componente escolhido (ex.: `switch.joker_icon_1` só existe na pílula de treino, mas as telas de rallycross o citam), e itens `screen_header` em telas cuja cena não tem esse nó.

## Visualizador (`tools/uiview`)

```
python -m tools.uiview [--game PASTA] [-o build/uiview] [--models 037,phil_mills] [--all-models] [--open]
python3 scripts/export_all_models.py [--game PASTA] [-o build/uiview]
```

Lê o jogo (só leitura) e gera `build/uiview/index.html`. Se o navegador bloquear `file://`, sirva a pasta por HTTP. A saída confirmada em 2026-10-05:

- `data/ui.js` — 288 telas, estados, fluxo, estilos, 415 diálogos e os ids de tela que o executável cita
- `data/scenes.js` — cenas de `persistentDB`, `d_osd` e `loadingScreen`, com malhas, materiais e curvas
- `data/strings.js` — inglês (12597 chaves) e português (12594)
- `data/assets.js`, `img/` e `thumb/` — 2678 WebP da interface. Entram os bundles `frontend/bundles/*.pssg` (interface, HUD, fontes, locais e carros) e os `frontend/streamed_textures/**/*.tpk` que têm bloco de dados (188 telas de carregamento, 172 traçados, 158 pinturas). Dos 1148 `.tpk` listados, 518 têm bytes; os `lg_*` de pintura de equipe são entrada de diretório sem payload
- `data/models.js` e `models/*.bin` — índice 3D. A geometria é little-endian, magia `DR2M`. A exportação de 2026-10-05 com `--models 037,phil_mills` indexou 543 PSSG e gravou malha de dois: `037_highLOD` (20 malhas, 90439 vértices) e o copiloto Phil Mills (12 malhas, 20015 vértices). As texturas desses dois somam 39 WebP a mais na galeria (2717 no total dessa rodada)

- **Telas:** busca por id, título ou texto e filtro por situação: com estado no fluxo, citada por outra tela, nome no executável, ou sem referência. Desenha em 1920×1080 com as regras acima. O inspetor mostra o objeto, a cena, a tela-mãe, os estados e os eventos do fluxo, cada item (texto, dados, evento, condição, glyphs que não existem na cena) e o XML. Clicar num item ou usar as setas põe o item em foco (fim do `activate`).
- **Cenas:** qualquer raiz das três bases, com a árvore de nós e quem a inclui.
- **Imagens:** grade com miniatura (`thumb/`) e, no painel, o arquivo em tamanho real (`img/`). Filtro por grupo. O painel lista cenas e telas que citam o nome da textura.
- **Modelos:** lista o índice (carro, interior, LOD baixo, personagem, genérico, local, prop). Quem tem `models/*.bin` abre um preview WebGL. O botão esquerdo orbita nos dois eixos, no sentido de agarrar o modelo (arrastar para a direita gira o modelo para a direita; o arrasto vertical inclina de verdade). Botão direito, do meio ou Shift+esquerdo arrasta o alvo no plano da tela. A roda aproxima. O piso troca asfalto/terra/neve (as rodas alternativas ocupam o mesmo lugar e ficam ocultas até a escolha). O hash é `#m=<id>`. `fetch` do `.bin` pede HTTP (`python -m http.server` dentro de `build/uiview`).
- **Textos:** abre já nos textos usados pelas telas (até 300 linhas). A busca, a partir de 2 letras, olha a chave e o texto nas duas línguas. "Sem português" são as chaves que só existem no inglês.
- **Diálogos:** título, corpo e opções de `message_dialogs`, e os estados que citam o id. Chave que não está em nenhuma das duas línguas aparece como `[sem texto: chave]`, não como se o id fosse a frase.
- **Animação:** a barra debaixo do canvas toca os eventos (`open`, `close`, `activate`, …) na tela inteira ou só no item em foco, com velocidade e um quadro arrastável. Repouso volta ao fim do `open` (ou ao `defaultFrame`; o item em foco usa o fim do `activate`). O inspetor desenha as curvas quando a lista abre. "Mostrar ocultos" pinta em magenta tracejado o que a cena tem e o quadro esconde. **Ocultos agora** diz o motivo: alfa 0 neste quadro, escondido pelos dados da tela, posição de lista sem item, condicional, fora da rolagem, variante de estado (desabilitado) ou outra opção do switch. **Variantes** troca o filho escolhido do switch.
- **Idioma:** o seletor troca os rótulos do visualizador e os textos do jogo juntos. Chave ausente na língua escolhida sai com † e o texto da outra língua. Só três chaves do inglês não estão no português: `lng_esports_terms`, `lng_community_poll_subtitle`, `lng_demo_menu_title`. Nomes de evento que o visualizador não traduziu continuam com o id do jogo (o botão mostra esse id no título).
- **Opções:** caixas dos itens, mostrar ocultos, campos de dados (`‹data_path›` em laranja; o C++ preencheria), nomes dos nós, esconder condicionais, lista inteira, sem animação (valores gravados no material, sem avaliar as curvas) e fundo de jogo.

Os pacotes `cars/*.nefs` e `locations/*.nefs` não começam com a magia NeFS. Os primeiros 128 bytes são um bloco RSA-1024 (expoente 65537, módulo público do DiRT Rally 2.0, inteiro little-endian). O resto do cabeçalho é AES-256-ECB; a chave são 64 caracteres hex em claro depois dessa abertura. `NefsArchive.open_path` faz isso e segue o leitor que já existia. `037.nefs` (110 MB) abre assim: 71 arquivos, quase todos PSSG. A pintura do carro está em `cars/models/<id>/livery_00/textures_high/*.pssg` (o `037_tex_high_00.pssg` tem ~85 MB). `livery_user` fica de fora. Sem `--models`, a malha dos arquivos principais de até 40 MB é exportada. `--all-models` (ou `python3 scripts/export_all_models.py`) abre um pacote por vez e grava a malha de tudo que cabe em 120 MB: carros, personagens, locais, props, interiores e LOD baixo. Imagem WebP e `models/*.bin` que já existem são reaproveitados; o PSSG da malha não é lido de novo. Acima de 120 MB ninguém é aberto: o terreno `tracksplit.pssg` (~2 GB) e qualquer outro arquivo nesse tamanho ficam só no índice, com a linha `só índice, acima de 120 MB` no log. Um `python -m tools.uiview` sem `--all-models` regrava o índice com a regra dos 40 MB.

A pintura do 037 (número 2, Magneti Marelli, Bilstein, speedline, Würth) está no trecho certo do atlas `037_main_d`. O V da malha já segue o DirectX: com `UNPACK_FLIP_Y_WEBGL` em 0, `v = 0` amostra o topo da imagem, e o número 2 da porta direita casa com o 2 em pé do atlas (correlação da máscara do dígito ~0,65 contra ~0,29 do espelho). A porta esquerda usa a cópia já invertida do mesmo trecho e também sai com o 2 para a frente. Espelhar o U dentro da ilha (`u' = umin + umax - u`), ou fazer `u = 1 - u` no atlas inteiro, vira o 2 ao contrário ou troca de ilha. Virar o V faz o mesmo com o outro eixo. O preview não mexe no UV da lataria; a caixa «Virar V» fica só para o arquivo em que o jogo gravou o V invertido, e começa desligada.

### Roda, pneu e disco de freio

Medido no `037_highLOD` e no `es2_highLOD` (2026-10-05). O preview só lê o difuso. Normal, especular e opacidade ficam na galeria e não entram no WebGL.

As texturas da pintura estão em `cars/models/<id>/livery_00/textures_high/`. O pacote `*_tex_high_00.pssg` é a lataria e não traz a roda. Os quatro pisos (`*_tex_tm_`, `_gr_`, `_sn_`, `_wt_`) repetem o mesmo id `%s_wheel_d.tga`. A exportação grava o piso no nome (`037_wheel_d_tm.tga`) para um não apagar o outro. `livery_user` fica de fora. O `SHADERINSTANCE` da roda não guarda o binding: `parameterSavedCount` é 10 e só há constantes (`EnvironmentColour`, Fresnel, especular). O jogo amarra `%s_wheel_d.tga` em runtime. `texcoord4`–`texcoord7` existem no `car_wheel.fx` / `car_tread.fx` / `car_disc.fx` e não vêm gravados na instância.

O stream de UV é `ST` `half4`. O exportador usa os dois primeiros halves. No disco e no `car_disc_blur` há um segundo `ST` `half2`; no 037 ele é constante `(0, 1)` e não é o difuso. Entra o LOD0 (`MATRIXPALETTERENDERINSTANCE`). `MATRIXPALETTEJOINTRENDERINSTANCE` repete a malha e fica de fora. Um float de `SkinIndices` escolhe o osso; a matriz do PSSG é vetor-linha, translação em 12, 13 e 14.

O atlas `*_wheel_d` do 037 e do es2 é 1024² e tem o mesmo desenho:

| Região | Retângulo | O que é |
| :--- | :--- | :--- |
| Foto da face de fora | o círculo no centro | lateral com letra, rodão e porcas |
| Cartão de parafusos | canto inferior esquerdo, por volta de u &lt; 0,32 e v &gt; 0,75 | dois parafusos em close |
| Rótulo | x 0–320, y 944–1024 | a palavra «TREAD» e uma barra cinza. O sulco está no normal dessa barra |
| Borracha | x 128–384, y 868–932 | faixa escura (luminância ~40–55 nos dois carros), à direita dos parafusos e acima do rótulo |

`tarmac_tread` (e terra/neve) não tem textura com «tread» no nome. O U dá voltas na banda (cerca de 0 a 4,3 no es2) e o V vai de −1 a 0. Sem recorte, o nome não casa com `wheel_d` e a banda recebia `*_main_d`. Amostrar o rótulo escreve «TREAD» na borracha. O preview manda `fract(U)` e `fract(V)` para x 128–384, y 868–932. O sulco continua invisível: ele está no normal, e o preview não o usa.

A face de fora de `tarmac_wheel` fica com o UV gravado, o da foto. Os triângulos do miolo cujo UV cai no cartão de parafusos (u &lt; 0,32, v &gt; 0,75) e cujo raio é menor que 0,36 do raio da roda são descartados, para o disco aparecer no vão. O anel que amostra o centro do atlas permanece: é o rodão.

A lateral de dentro reaproveita o atlas inteiro (letra, código de barras, centro branco) e alguns triângulos saltam para o canto, então por trás o pneu parece um cartaz. No es2 essa face está em x ≈ −0,54 e a face de fora em x ≈ −0,79, uns 25 cm para dentro. O preview trata como lado de dentro o triângulo cujo centro está a mais de 10 cm da face externa, na direção do meio do carro, e põe nele a mesma borracha da banda. Vértice compartilhado com a face de fora é clonado, para a foto externa não ser puxada.

`discs` é o disco de verdade: chapéu, face e espessura, na posição do modelo. O preview não achata. O UV gravado da borda (no es2, raio ~0,15) espalha o atlas inteiro, faixa e círculos soltos inclusos. A face interna já cai perto do centro. O preview recalcula todo o UV pelo ângulo e pelo raio: centro 0,5 e raio 0,40 vezes `r / rmax`. No `*_disc_d` (256²) do 037 e do es2 o rotor colorido chega a ~0,36–0,38 do centro (percentil 85); 0,33 só mostrava o miolo bege. `car_disc_blur` é o cartão do disco girando. No 037 o `EnvironmentColour` é `(0, 0, 0, 1)` e a malha fica por fora da face (x ≈ −0,87 contra ≈ −0,84). Opaco, cobre o rodão. O preview não desenha material com `disc_blur`. O es2 não tem esse material.

O que o jogo faz no shader para achar a faixa de sulco não está no PSSG. O retângulo de borracha é a aproximação do preview para o difuso, conferida no 037 e no es2.

O preview usa o LOD0 do carro, com um osso por vértice. No personagem o vértice `SkinnableVertex` já está na pose de repouso e as cópias `_x2`/`_x3` são descartadas. Não há playback de animação esquelética.

Limites: o texto usa Barlow Condensed e Roboto Condensed no lugar das MSDF do jogo. Dados preenchidos pelo C++ (`tabs.info`, listas, imagens de `BTextureData`) aparecem como caminho ou vazios. A distorção de tempo das curvas é ignorada: entre duas chaves o quadro anda em linha reta. Algumas texturas de local já saem do jogo como um quadro cinza com a palavra PLACEHOLDER; o visualizador só as decodifica.

O `egodata` tinha um bug, corrigido nesta rodada: um bloco cifrado podia passar por deflate válido por acaso e render poucos bytes de lixo. Isso cortava, por exemplo, ~190 KB do `b_persistent.pssg`. Agora só vale a descompressão que dá o tamanho esperado do bloco.

## HUD da corrida

`d_osd.pssg` usa o mesmo sistema. Raízes com uso possível para um mod: `osd/component/notification_feed/notification_stackers`, `osd/unit/notification_feed/lobby_message`, `osd/unit/notification_feed/penalty_message`, `osd/component/warning_message/warning_message`, `osd/component/dirt_academy/da_telemetry` (barras de acelerador, freio, embreagem e volante), `osd/component/position_table/*`, `osd/component/progress_bar/*`, `osd/component/timings/*`. `osdSetup.xml` e `osdParams.xml` (`game_1.dat`) configuram o HUD; não foram decodificados nesta passada.

## Classes de renderização no executável

Registradas pelo mesmo `0x1408c5c30` e úteis para desenhar fora das cenas:

| Classe | String | Registro |
| :--- | :--- | :--- |
| `NeBlitterInstanceTex` / `NeBlitterInstanceUntex` | `0x1412d3650` / `0x1412d3860` | `0x140077dd3` / `0x140077e23` |
| `QuadRenderInstance` | `0x1413a6bb8` | `0x1400840d3` |
| `DebugRendererRenderInstance` | `0x1412d36a0` | `0x1400772d3` |
| `NeVectorTextNode2d`, `NeModelTextNode2d`, `NeTextNode` | `0x1412d12f8` (vetorial) | `0x140078553` |
| `NeTextLayout`, `NeGlyphManager` | `0x14123ced0`, `0x1412d34e0` | `0x140078325`, `0x140077fe3` |

O blitter monta uma base de dados própria (`blitter_database_%03d`, referência em `0x14088df48`) com `blitter_root`, `blitter_camera`, `blitter_render_node` e duas instâncias, `blitter_render_instance_tex` e `_untex`. Os shaders são `TexturedBlitter` / `blitter_instance_tex_shader` (`0x140880c4a`, `0x140880d34`) e `UntexturedBlitter` / `blitter_instance_untex_shader` (`0x140880e18`). CONFIRMADO pelas strings e referências; a API de chamada ainda não foi lida.

## Caminhos para desenhar de forma nativa

Do mais barato para o mais caro:

1. **Reusar telas e componentes por dados** (já feito): `screens.bin` + `flow.bin` + `states.bin` com os moldes existentes. Limites em [UI Limits](ui_limits.md).
2. **Mudar estilo no carregamento**: o detour de `0x140c54270` (`src/core/ui_data.cpp`) recebe todo documento. Se `text_styles.xml` passar por ele, dá para criar estilos novos (outro tamanho, tracking, quebra) e usá-los com `{s:<id>}` num `UINODETEXTDOC`. HIPÓTESE: no log de 2026-10-05 ele não apareceu, mas o registro corta em 64 documentos; aumentar o limite e conferir.
3. **Mudar a cena no carregamento**: `src/core/pssg_patch.cpp` já troca bytes do `persistentDB.pssg` durante a leitura. Com a mesma técnica dá para trocar valores do mesmo tamanho: cor (`DiffuseColour`), alpha, `TRANSFORM`, `sn`/`wi` de um texto. Acrescentar nós (um componente novo) muda o tamanho e exige entregar o arquivo inteiro à leitora. HIPÓTESE.
4. **Mudar parâmetros em runtime**: a animação resolve `matinst=<material>#<parâmetro>` por nome. A mesma resolução daria cor e alpha dinâmicos a qualquer material (piscar, mudar de cor conforme um valor). Falta achar a função que resolve o nome. HIPÓTESE.
5. **Desenhar por conta própria com o blitter**: quads com e sem textura numa câmera 2D do motor, dentro do pipeline D3D11 do jogo (ordem de desenho e resolução corretas, sem overlay externo). Falta ler a API em `0x14088df48`/`0x140880c4a` e achar quem chama. Para texto, `NeTextLayout` + `NeGlyphManager` com as fontes MSDF do jogo. HIPÓTESE.

## Perguntas em aberto

| # | Pergunta | Como testar |
| :--- | :--- | :--- |
| 1 | `text_styles.xml` passa pelo detour de `0x140c54270`? | Subir `kMaxRecords` em `ui_data.cpp` ou filtrar por `frontend/configs/` e ler o log. |
| 2 | Quem chama o blitter e com quais argumentos? | Xrefs das funções que usam `blitter_root`; hook de log no frame de uma tela que desenhe quads (provável: editor de pintura, `liveryEditor_*`). |
| 3 | Qual função resolve `matinst=…#…`? | Procurar quem lê o separador `#` perto dos `NeAnimTargetName`/`NeAnimSceneBinding`. |
| 4 | Formato BXML `\1BXM` | Comparar `value_display_helpers.xml` com o decodificador acima. |
| 5 | Mudar a cor do destaque da pausa | Patch de mesmo tamanho no `DiffuseColour` de `matinst=smart_pill_text!69` ou no pacote `packet5237`; F8 não recarrega a cena, precisa reiniciar o jogo. |
