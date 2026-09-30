# Dados da UI: telas, estados e fluxo

As telas do frontend, os estados que as abrem e as transições entre elas não estão no executável. Não há Scaleform nem XML solto: são três arquivos binários dentro de `game/game_1.dat`. Esta página descreve onde eles ficam, o formato e o que eles dizem sobre o menu de pausa.

Nada aqui foi escrito na pasta do jogo nem no processo. A ferramenta `tools/egodata` só lê.

## Pacotes

Base: a pasta de instalação do jogo.

| Pacote | Cabeçalho | Conteúdo de UI |
| :--- | :--- | :--- |
| `game/game.nefs` | No início do próprio arquivo | Fontes, `frontend/configs/*.xml`, bundles de pista e de veículo |
| `game/game.dat` | Embutido no `dirtrally2.exe` | Texturas de frontend, `stats`, `network` |
| `game/game_1.dat` | Embutido no `dirtrally2.exe` | `system/screens.bin`, `states.bin`, `flow.bin`, `links.bin`; `frontend/databases/persistentDB.pssg`; `frontend/message_dialogs/*.xml`; `language/*.lng` |
| `game/game_2.dat`, `game/game_2_1.dat` | Embutido no `dirtrally2.exe` | Só `tracks/locations` |

No executável da versão atual, os cabeçalhos embutidos começam nos offsets de arquivo `0x10b3fe0`, `0x10bed68`, `0x10de1f0` e `0x10f1678`. CONFIRMADO: magia `NeFS`, versão `0x20000`, chave em hex. A ferramenta não usa esses números: ela procura a magia no executável e escolhe, para cada `.dat`, o cabeçalho cujo último bloco termina junto com o fim do arquivo.

## Formato NeFS v2.0

CONFIRMADO por extração.

| Offset no cabeçalho | Conteúdo |
| :--- | :--- |
| `+0x00` | Magia `NeFS` |
| `+0x24` | Chave AES-256, 64 caracteres hex em ASCII |
| `+0x64` | Tamanho do cabeçalho |
| `+0x68` | Versão, `0x20000` |
| `+0x84` | Oito offsets de partes, na ordem P1, P6, P2, P7, P3, P4, P5, P8 |

- **P1** tem um item a cada 20 bytes: `u64 offset no volume, u32 índice em P2, u32 índice em P4, u32 id`.
- **P2** tem uma entrada de diretório a cada 20 bytes: `u32 pai, u32 primeiro filho, u32 offset do nome, u32 tamanho, u32 id`. Um arquivo é a entrada cujo primeiro filho é ela mesma. A raiz é a entrada que é o próprio pai.
- **P3** é a tabela de nomes, terminados em zero.
- **P4** guarda, por item, os fins acumulados de cada bloco.

Cada bloco descomprime para 64 KiB. Em `game.nefs` o bloco é deflate cru. Nos `.dat` o bloco é AES-256-ECB com a chave do cabeçalho, e depois deflate cru. O AES da ferramenta passa no vetor FIPS-197.

## XML binário (`system/*.bin`)

CONFIRMADO: decodificar e recodificar reproduz os quatro arquivos byte a byte.

O arquivo é uma sequência de blocos `u32 tipo, u32 tamanho, dados`:

| Tipo | Conteúdo |
| :--- | :--- |
| `0x7252221a` | Raiz; contém todos os outros |
| `0x72522217` | Tabela de strings; contém os dois seguintes |
| `0x7252221d` | Strings terminadas em zero, com o bloco alinhado a 16 bytes |
| `0x7252221e` | `u32` de offset de cada string |
| `0x7252221b` | Nós, seis `u32` cada: nome, texto, nº de atributos, 1º atributo, nº de filhos, 1º filho |
| `0x7252221c` | Atributos, dois `u32` cada: chave e valor (índices de string) |

Os filhos de um nó ficam contíguos. As strings são internadas em pré-ordem, com o texto de um nó depois dos filhos.

| Arquivo | Tamanho | Nós | Papel |
| :--- | :--- | :--- | :--- |
| `system/screens.bin` | 1 642 392 B | 31 159 | Telas, itens e comportamentos |
| `system/flow.bin` | 947 840 B | 20 318 | Grafo de fluxo: `node` e `link` |
| `system/states.bin` | 72 264 B | 1 086 | Estados, ligados às telas pelo nome |
| `system/links.bin` | 368 B | 5 | Tipos de link: `back`, `switch_focus`, `reset_focus`, `return_from_imposed` |

Os visuais (`fe/screens/smart_hub/smart_hub`, `fe/component/smart_hub/smart_hub_button_library`, `fe/component/smart/smart_set`) estão em `frontend/databases/persistentDB.pssg`, formato PSSG. Os textos traduzidos estão nos `.lng`, magia `LNGT`.

## O menu de pausa nos dados

### Tela (`screens.bin`)

Trecho, com itens omitidos:

```xml
<Screen id="pause_menu" glyph="screen_directory.screen_choice" object="smart_hub">
  <items>
    <Item id="item_7" glyph="smart_set.7.switch" enable_cursor="true">
      <IBStackItem/>
      <BSwitchStatic object="smart_hub_button_5col"/>
      <BVisibilityControlStatic glyph="save_dot" visible="false"/>
      <BTextStatic string="lng_options" glyph="text_title"/>
      <IBSelectableSimple out_data_path="event" select_value="options" help_text="lng_select"/>
      <IBAnimated/>
    </Item>
    <Item id="item_9" ...>
      <BTextStatic string="lng_vr_reset_view" glyph="text_title"/>
      <IBSelectableSimple out_data_path="event" select_value="reset_view" .../>
      <BVisibilityControlData data_path="reset_view_available" watch_data="false"/>
    </Item>
  </items>
  <behaviours>
    <SBAudioNotification screen_name="pause_menu"/>
    <SBScreenTitle string_id="lng_pause_menu_title" override_format_id="localise"/>
    <SBHotButtonScreenEvent action="Back" event_primary="back" data_path="event" .../>
    <SBItemStack glyph="smart_set.stacker"/>
    <SBGridItemFlow wrapV="true">item_0 ... item_10</SBGridItemFlow>
  </behaviours>
</Screen>
```

Isso fecha o que [Menu](menu.md) tinha visto em memória:

| Dado | Objeto em runtime |
| :--- | :--- |
| `BVisibilityControlData data_path="reset_view_available"` | Condição de vtable `0x1413d3100`, `+0xa0` = `ui.pause_menu.reset_view_available`, byte `+0x28` |
| `BTextStatic string="lng_vr_reset_view" glyph="text_title"` | Binding de texto estático, vtable `0x1413d2e58` |
| `BTextData ... format_id="explicit"` (itens 1, 4 e 10) | O `explicit` do binding de texto, `+0x30` |
| `IBSelectableSimple out_data_path="event" select_value="..."` | Selecionar o item grava o evento no caminho `event` |

Os `data_path` são relativos à tela: em runtime, viram `ui.pause_menu.<data_path>`.

### Estado (`states.bin`)

```xml
<StatePauseScreen id="314506569" screen_name="pause_menu"/>
<StateScreenFECore id="3411174356" screen_name="options_ingame"/>
```

`StatePauseScreen` também aparece com `id="589096572"` e `pre_race="true"`. `StateScreenFECore` é o estado genérico que só abre uma tela. Ele é usado seis vezes, e `options_ingame` é uma delas.

### Transições (`flow.bin`)

O grafo é hierárquico. Cada `link` tem como `id` o nome de um evento e aponta para outro nó:

```xml
<node id="129712173" state="2192346083">                 <!-- StatePause -->
  <node id="135287375" state="1837755473">
    <node id="231066979" state="314506569">              <!-- pause_menu -->
      <!-- 7 nós filhos: diálogos de confirmação -->
      <link id="continue" target="228227392"/>
      <link id="restart_race" target="178044564"/>
      <link id="options" target="254662489"/>
      <link id="quit" target="249609351"/>
      <link id="back" target="228227392" type="back"/>
      <!-- também objectives, recover_vehicle, retire_from_event, end_session_pvp,
           return_to_service, quit_retire, leave_game -->
    </node>
  </node>
  <node id="254662489" state="3411174356">               <!-- options_ingame -->
    <link id="game_settings" target="225375566"/>
    <link id="graphics_calibration" target="239911392"/>
    <link id="audio_calibration" target="189727922"/>
    <link id="input_calibration" target="160851369"/>
    <link id="back" target="231066979" type="back"/>
  </node>
</node>
```

O alvo de um link pode estar em outro ramo do grafo: `options_ingame` é filho de `StatePause`, dois níveis acima do nó da pausa.

Há 33 nós de pausa no grafo, um por modo de jogo: 25 com o estado `589096572` e 8 com `314506569`. Todos têm um link `options`. Nenhum tem link `reset_view`. Isso bate com o dispatcher `0x140285640`: ele consome `reset_view` e os outros eventos que trata, e o resto cai na busca de links do nó atual.

## Do evento à tela, no executável

Análise estática, com a resolução de `options` conferida por leitura de memória com a pausa aberta. Nada foi escrito.

1. **Callback da UI.** `0x1402d5ad0`, inscrito no nó `event` pelo Enter do estado (`0x1402d2040`, slot `+0x18`), chama o dispatcher `+0x88`. Se ele devolve falso, salta para `0x140215a80(estado, nome)`.
2. **Transição pendente.** `0x140215a80` copia o nome para a string em `estado+0xd8` (buffer inline em `+0xe8`, inicializado pelo construtor base `0x1402c2d40`).
3. **GetTransition.** O slot `+0x70` do estado de tela, `0x1402d5730`, devolve essa string. O identificador é um `const char*` e a comparação (`0x140c79680`) é `strcmp`, sem hash.
4. **Runner do fluxo.** `0x140c7bd30` (slot `+0x18` da vtable `0x141249898`) pede a transição ao estado e chama `FindLink(nome)` em cada nó da pilha, do topo para baixo. O link devolve `{int target, char* type}`. O `type` escolhe um handler num mapa em `runner+0x20`. Sem link em nenhum nó, só `_discardparent` tem tratamento.
5. **Tick.** `0x140c7c4b0` aplica o destino: sem handler especial, `0x140c7c1c0(pilha, idNó)` acha o nó no grafo (`0x140c7b250`) e troca de estado. No fim, zera `runner+0x100`, `+0x48` e `+0x70`.
6. **Enter do novo estado.** `0x1402d2040` monta `"ui." + screen_name`, inscreve o callback e chama `0x140d32200(gerenciadorUI, &handle, screen_name, flag)`. Este faz FNV-1a do nome e procura a tela em `gerenciador+0x68`. O handle fica em `estado+0x120`.

Com a pausa aberta, a pilha tinha 8 nós. O do topo usava o `StatePauseScreen` de `screen_name` `pause_menu`, com os links `continue`, `return_to_service`, `objectives`, `options`, `ghost_select`, `quit`, `quit_retire`, `back`, `abandon_challenge` e `end_session`. `options` levava a um nó com um `StateScreenFECore` (vtable `0x141256e98`) de `screen_name` `options_ingame`, cujo `back` voltava ao nó da pausa.

### Quem monta isso a partir dos dados

| Peça | Função | Observação |
| :--- | :--- | :--- |
| `flow::Coordinator` | `0x140c7bf90` | Carrega os blocos `flow`, `states`, `links` e `states_init`. Global em `[0x1416951e0]`. |
| Nó do grafo | `0x140c7ade0` | Atributos `jump_id`, `id`, `state`; filhos `link` com `id`, `target`, `type` (`0x140c789e0`, `0x140c78b00`). |
| Vtables de nó | `0x1413c9758`, `0x1413c97d8`, `0x1413c97e8` | Sem link, um link (`FindLink` `0x140c7b1e0`), vários links (vetor em `+0x20`, entradas de `0x18`; `FindLink` em `0x140c7b180`, não desmontado). |
| Registro de classes de estado | singleton `0x1415dae90`, registro `0x1401519b0`, criação `0x140246b90` | Vetor ordenado `{nome, thunk, criar}` com busca por `strcmp`. A tag de `states.bin` é o nome da classe; o `id` passa por `strtol` para `estado+8`. |
| Estados usados aqui | `StatePauseScreen` → `0x14028edc0`; `StateScreenFECore` → `0x1402da320` | O `screen_name` é lido por `0x1402cff80`. |
| Fábrica de `smart_hub` | `0x140d1f3f0(gerenciador, elemento, pai)` | Aloca `0xc8` bytes, construtor `0x140d10bd0` (vtable `0x1413d62d0`), carrega com `0x140d24220` e insere `{FNV-1a(id) → tela}` em `gerenciador+0x40`. Chamada pelo carregador do documento `World`, `0x140d33860`. |
| Salto por nome | `0x140c7c320` | Procura `jump_id` em `[runner+0x110, +0x118)` e grava `runner+0x100`. Só 11 `jump_id`, nenhum na pausa. |

As classes de estado não têm RTTI: a posição antes da vtable é código. Os descritores `.?AV` que existem são de templates e mostram o namespace `game::flow`.

## Onde os `.bin` são lidos

CONFIRMADO por análise estática e por leitura do processo, salvo onde indicado.

Os caminhos não estão no executável. Vêm de `system/boot_data.xml`, em `game.nefs`:

```xml
<xml processor="flow::Coordinator" filename="/data/system/flow%configsuffix%.bin" pool="UPDATE_TEMPORARY" userdata="flow"/>
<xml processor="flow::Coordinator" filename="/data/system/links%configsuffix%.bin" pool="UPDATE_TEMPORARY" userdata="links"/>
<xml processor="flow::Coordinator" filename="/data/system/states%configsuffix%.bin" pool="UPDATE_TEMPORARY" userdata="states"/>
<xml processor="ScreensData" filename="/data/system/screens%configsuffix%.bin" pool="UPDATE_TEMPORARY" />
```

`%configsuffix%` vira string vazia (`0x140391093` usa o byte nulo em `0x1411f44c5`).

1. **Boot.** A inicialização do app, `0x14038f740` (chamada por `0x1405f9cf0`), registra dois decodificadores de XML com `0x140c55180`: texto `0x140c51920` e binário `0x140c519f0`. Em `0x1403911a3` ela carrega `system/boot_data.xml` com `0x140c5ff80`. É a única referência à string.
2. **Um job por `<xml>`.** Job de `0x318` bytes, fábrica `0x140c58840`, construtor `0x140c59d30`, preenchido por `0x140c5ec60`.

   | Campo do job | Conteúdo |
   | :--- | :--- |
   | `+0x50` | `char*` do caminho |
   | `+0x170` | `char*` do `userdata` (derivado da vista do Coordinator; falta conferir ao vivo) |
   | `+0x278` | Alocador do pool |
   | `+0x288`, `+0x298` | Buffer cru e tamanho |
   | `+0x2a0` | Handle do documento: `{+8 alocador, +0x10 documento}` |
   | `+0x2b8` | Processador (`flow::Coordinator`, vtable `0x1412497d0`, ou `ScreensData`, vtable `0x1413d6ce8`) |

3. **Leitura.** `0x140c5c0c0` pede a leitura assíncrona (`0x14083c580`) com o caminho de `+0x50`.
4. **Documento.** `0x140c61e70`, quando a leitura termina, põe um NUL em `buf[tam]` e chama `0x140c54270(job+0x2a0, alocador, buf, tam+1)`. O endereço de retorno é `0x140c62176`.
5. **Processamento.** O slot `+8` do processador recebe a vista do job: `0x140c7bf90` no Coordinator, que escolhe o bloco pelo `userdata`, e `0x140d333e0` → `0x140d33860` no `ScreensData`. `World` em `0x140d33860` é só o rótulo de um escopo de alocação.

### O parser

| Função | Assinatura | Papel |
| :--- | :--- | :--- |
| `0x140c54270` | `int (handle*, alocador*, const void* buf, size_t tam)` | Zera o handle, tenta os decodificadores do último ao primeiro (binário antes do texto), grava o documento em `handle+0x10`. `0` é sucesso. |
| `0x140c519f0` | `doc* (alocador*, buf, tam)` | Decodificador binário: valida, aloca `0x40` bytes (vtable `0x1413c73e0`), chama o parser. |
| `0x140c54330` | `int (doc*, buf, tam)` | Único `cmp eax, 0x7252221a` do `.text`. |

O documento não copia os dados: `+0x10` strings, `+0x18` offsets, `+0x28` nós e `+0x38` atributos apontam para dentro do buffer. O destrutor `0x140c501b0` não libera o buffer.

Prólogos, iguais no arquivo e no processo:

| Função | Bytes |
| :--- | :--- |
| `0x140c54270` | `48 89 5c 24 20 56 48 83 ec 30 48 89 6c 24 40 48 8b f2 4c 89 74 24 50` |
| `0x140c54330` | `48 83 ec 28 4c 8b d1 49 83 f8 08 0f 86 26 01 00 00` |
| `0x140c519f0` | `48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 56` |

### Momento e persistência

Os quatro arquivos são lidos uma vez, no boot. Que isso não se repete no frontend nem no carregamento de especial é HIPÓTESE forte: o pool é `UPDATE_TEMPORARY` e não há outra referência a `boot_data.xml`. Com o jogo já iniciado, nenhum trecho de `screens.bin`, `flow.bin` ou `states.bin` estava na memória: o buffer cru é liberado depois do processamento.

### Ponto de injeção

Um detour em `0x140c54270` que só age quando o endereço de retorno é `0x140c62176`. O job é `handle - 0x2a0`, e o caminho em `job+0x50` diz qual arquivo é. O detour chama o original com um buffer da DLL (`len + 1`, com NUL no fim). O jogo continua liberando o próprio buffer. O nosso precisa viver até o fim do processo, porque o documento aponta para dentro dele.

O hook tem de estar ativo antes de `0x1403911a3`. A `dxgi.dll` chega pelos imports de `d3d11.dll`, antes de o jogo rodar, mas a thread de init da DLL faz proxy, core e hooks de `Present` antes. Por isso o hook de dados tem de ser instalado no `DLL_PROCESS_ATTACH` ou como primeira ação da thread.

O detour está em `src/core/ui_data.cpp`. Ele é instalado no `DLL_PROCESS_ATTACH`, depois de conferir o prólogo de `0x140c54270` e o `call` em `0x140c62171`. Para as chamadas do job, guarda caminho, `userdata`, tamanho, magia e o tempo desde o attach. A thread de init grava isso no log (`UiData: documento ...`) assim que abre o `dr2hook.log`.

CONFIRMADO no jogo, com o detour ainda só observando: o hook chega a tempo. Os documentos do job começam cerca de 2,1 s depois do attach, e o log já está aberto nesse momento. A ordem foi `links.bin` (369), `states.bin` (72265), `flow.bin` (947841) e `screens.bin` (1642393): o tamanho do arquivo mais o NUL. O `userdata` foi `links`, `states` e `flow`. Em `screens.bin`, que não declara `userdata`, o campo repete o caminho. Os offsets `+0x50` e `+0x170` estão certos. Pela mesma função passam também XML em texto (magia `<?xm`) e outro binário (`\x01BXM`), como `frontend/configs/*.xml` e `frontend/message_dialogs/*.xml`.

### Patch dos documentos

`src/core/bxml.cpp` decodifica e recodifica o XML binário em C++. Com os arquivos reais, a ida e volta sai byte a byte. `src/core/ui_patch.cpp` aplica os acréscimos numa cópia da árvore e só a devolve se tudo der certo. Qualquer falha ou exceção mantém o original e vai para o log.

Os documentos são processados em sequência na mesma thread, e a ordem muda de uma sessão para outra:

| Sessão | Ordem |
| :--- | :--- |
| 1 | `links`, `states`, `flow`, `screens` |
| 2 a 4 | `flow`, `links`, `states`, `screens`, que é a ordem do `boot_data.xml` |
| 5 | `links`, `flow`, `states`, `screens` |

Duas versões falharam por isso, sem dano. Na primeira, `flow` exigia `states` já alterado, então `flow` e `screens` ficaram originais. Na segunda, `flow` esperava `states` por até 3 s, mas `states` só é interpretado depois de `flow` voltar, e o boot atrasou 3 s. Nas duas, só o estado novo entrou, sem uso, e o item seguiu pelos hooks de reserva.

Agora cada patch depende só do próprio documento:

- Os ids dos estados são fixos, `1146224640` e `1146224641` (`0x44520000` e `0x44520001`), livres em `states.bin` e em `flow.bin`. Cada patch recusa se um id ou um nome de tela já estiver em uso.
- `flow` põe o link em todo nó com link `options`: 80 nos dados atuais, dos quais 33 são de pausa. Os demais são área de serviço, resultados e o estado de jogo cujo `options` leva à gestão de dispositivos. Nesses nós o link fica inerte, porque só o item da pausa dispara `dr2hook`.

O detour só compara decisões já tomadas. `flow` fica original se `states` já foi mantido original. `screens` fica original se `states` ou `flow` já foram mantidos. O único caso não coberto é `states` falhar depois de `flow` ter sido alterado; o log marca `INCONSISTENTE`. `test_ui_patch` aplica os três patches nas seis ordens e exige o mesmo resultado.

| Documento | Acréscimo |
| :--- | :--- |
| `states.bin` | Dois `StateScreenFECore`: `1146224640` com `screen_name="dr2hook_hub"` e `1146224641` com `dr2hook_mod`. As páginas das abas não têm estado. |
| `flow.bin` | Em cada nó de origem `P` com link `options`, um `<link id="dr2hook" target=H/>` e dois nós novos no mesmo pai do alvo de `options`. `H` (hub com abas) tem `dr2hook_nav_mod_0` a `_23` para `M` e `back` para `P`. `M` (tela do mod) tem `back` para `H`. Os ids são os primeiros livres a partir de `0x0d520000`, abaixo de `2^28`. |
| `screens.bin` | Na `pause_menu`, o item do evento `reset_view` passa a disparar `dr2hook`, ganha `BTextStatic string="DR2 Hook" explicit="true"` e perde o `BVisibilityControlData`. No World de `graphics_calibration` entram o host `dr2hook_hub`, as páginas `dr2hook_page_main` e `dr2hook_page_mods` e a tela `dr2hook_mod`, descritos em [Telas do DR2 Hook](#telas-do-dr2-hook). |

`explicit="true"` em `BTextStatic` é atributo que o jogo já usa (468 ocorrências). Não vale para `SBScreenTitle`, que não tem filhos.

Um arquivo `dr2hook_ui_patch.disabled` ao lado da `dxgi.dll` desliga o patch; o detour continua registrando os documentos. Os testes estão em `tests/test_ui_patch.cpp`. Com `DR2_UI_DIR` apontando para os `.bin` extraídos, eles conferem a ida e volta e o patch com os dados reais; `DR2_UI_OUT` grava o resultado.

Como o parser tenta o decodificador de texto quando o binário falha, entregar XML texto no lugar do `.bin` deve funcionar. HIPÓTESE, não testada.

## Ferramenta

```bash
python3 -m tools.egodata list game_1.dat system/
python3 -m tools.egodata extract game_1.dat system/screens.bin -o /tmp/ui/screens.bin
python3 -m tools.egodata xml /tmp/ui/screens.bin -o /tmp/ui/screens.xml
python3 -m tools.egodata roundtrip /tmp/ui/*.bin
```

`--game` troca a pasta de instalação. Os testes (`python3 -m unittest tools.egodata.tests.test_egodata`) não leem os arquivos do jogo.

## Caminho para uma tela própria

Com os dados, uma tela nova é uma questão de três acréscimos:

1. Um estado `StateScreenFECore` com um id livre e `screen_name="dr2modloader"`, em `states.bin`.
2. Uma `<Screen id="dr2modloader" object="smart_hub">`, em `screens.bin`, copiada de `options_ingame`.
3. Em cada nó de pausa de `flow.bin`, um `link` com o evento do nosso item. Ele aponta para um nó novo com esse estado, no mesmo pai do alvo de `options`, e com um `back` de volta à pausa.

CONFIRMADO no jogo, com o patch da seção anterior. Nessa sessão a ordem foi `flow`, `links`, `states`, `screens`, e o log registrou `alterado` nos três documentos: `link em 80 no(s) com options`, `estado 1146224640` e `tela dr2modloader`. Selecionar "DR2 ModLoader" na pausa abriu a tela nativa `dr2modloader` em vez do overlay. O evento do item tem link no grafo, o dispatcher nativo não o trata, e o runner abre a tela sozinho. O hook de `+0x88` deixa de ser necessário para a navegação e fica só como reserva.

### Eventos da tela

Esta seção e a seguinte descrevem a primeira versão, com uma tela `dr2modloader` e ids `dr2modloader_*`. A versão atual está em [Telas do DR2 Hook](#telas-do-dr2-hook).

| Botão | Evento | Quem trata |
| :--- | :--- | :--- |
| Open overlay | `dr2modloader_open_overlay` | DLL: o core abre o overlay no próximo quadro |
| Reload Lua mods | `dr2modloader_reload_mods` | DLL: o core chama `ModManager::ReloadMods` |
| Reload native core | `dr2modloader_reload_core` | DLL: o mesmo pedido do F8 |
| Back | `back` | Fluxo: o link `back` do nó novo volta à pausa |

Análise estática, ainda não validada no jogo. O slot `+0x88` da vtable do `StateScreenFECore` (`0x141256e98`, slot em `0x141256f20`) aponta para `0x140ebb300`, um stub `xor al, al; ret` compartilhado com outras classes. Por isso a DLL troca o ponteiro na vtable em vez de desviar o código. A vtable só é usada pelo `StateScreenFECore`: das 115 classes que passam pelo construtor base, as outras 114 gravam uma vtable própria logo depois. O callback `0x1402d5ad0` limpa a transição pendente, chama o slot com `(estado, const char* nome)` e só chama `0x140215a80` quando o slot devolve falso. O detour (`src/core/native_screen.cpp`) só age quando `estado+8` é `0x44520000` e o nome começa com `dr2modloader_`; devolve verdadeiro e não deixa transição pendente. O resto vai para o stub. Antes de trocar, a DLL confere o valor do slot e os bytes `32 c0 c3` do stub.

Primeiro teste no jogo, em 30/09. O log registrou `NativeScreen: eventos da tela dr2modloader tratados pela DLL.` e os três documentos alterados. O usuário relatou um problema: o cursor não descia para selecionar os outros itens. Nenhum evento `dr2modloader_*` apareceu no log, mas essa versão só registrava evento sem tratador. A versão seguinte registra `NativeScreen: evento <nome>` a cada botão tratado. No teste seguinte, com um id por linha, o cursor desceu pelos quatro botões, e o log registrou `NativeScreen: evento dr2modloader_open_overlay`. Segundo o usuário, só o título continuava "Opções".

A causa do cursor está no formato da grade, e não no tratador. O texto de `SBGridItemFlow` descreve a grade de navegação: cada linha do texto é uma linha da grade, e ids separados por espaço na mesma linha viram colunas. CONFIRMADO pelos dados:

- `options_ingame` e `pause_menu` têm um id por linha (`\r\n item_0\r\n item_1 ...`).
- Das 82 grades de `screens.bin`, 15 têm mais de um id por linha, como `spare_new0 spare_new1 spare_new2 spare_new3`.

O patch montava `" item_0 item_1 item_2 item_3 "`, uma linha com quatro colunas, e por isso para baixo não tinha destino. HIPÓTESE: esquerda e direita teriam trocado de item. Agora o patch gera um id por linha, e `test_ui_patch` exige isso. O parser da grade no executável não foi desmontado.

`back` já é o evento do botão de voltar do controle (`SBHotButtonScreenEvent event_primary="back"`), gravado no mesmo `data_path="event"`. Por isso um item com `select_value="back"` deve voltar pelo mesmo link.

### Título

O título da tela nativa vem de uma chave própria, `lng_dr2modloader_title`, que a DLL responde na busca da tabela de idioma. CONFIRMADO no jogo: o título mostrou "DR2 MODLOADER".

- Sem `override_data_path`, `SBScreenTitle` lê o `string_id` (RefString em `+0x50`) em `0x1401b4b70` e chama `0x140d010c0(RefString** saída, provider, const char* chave)`.
- O provider é o serviço `neon::NeLanguageStringHandler` (índice em `[0x142018ba8]`, vtable `0x14126a2f0`). O slot `+8` é `0x1403a8820`: `const char* Lookup(handler, const char* chave)`. Ele devolve um ponteiro UTF-8 para o texto dentro da tabela carregada, ou nulo. Ao vivo, `lng_options_ingame_title` deu `OPÇÕES`, `lng_pause_menu_title` deu `MENU DE PAUSA`, e `lng_dr2modloader_title` não existe.
- `0x140d010c0` copia o texto devolvido para uma RefString nova (`0x140d36360`), com o alocador do jogo. Por isso a DLL pode devolver um buffer estático sem alocar nada.
- Com a chave ausente e o flag `[0x14169539c]` em `0`, o jogo mostra `<lng_dr2modloader_title>`. Se isso aparecer, o hook não foi instalado.
- O formato `localise_upper` busca a chave com o sufixo `_caps`; a DLL responde as duas.

O patch troca só o `string_id` do `SBScreenTitle` da tela clonada. A DLL (`src/core/native_screen.cpp`) põe um hook MinHook em `0x1403a8820`, depois de conferir 24 bytes de prólogo (`48 89 5c 24 10 48 89 6c 24 18 56 48 83 ec 20 48 8b 99 88 00 00 00 48 8b`). O texto é `DR2 MODLOADER`, em maiúsculas como os outros títulos. A busca é chamada a cada texto traduzido, então o filtro olha o primeiro caractere antes do `strcmp`.

Caminho descartado: gravar a string `ui.dr2modloader.<caminho>` no modelo de dados, com `override_data_path` e `override_format_id="explicit"`. `0x140d08770` recebe um binding e uma RefString, não um caminho. `0x14011eba0` copia o texto com o alocador do store (`0x14011eaa0`), e `0x140816210` é o "último erro" do jogo em TLS, não uma tag de memória. Funcionaria chamando o setter com o binding do override (`SBScreenTitle+0x40`, acessor em `[+8]`, caminho internado em `acessor+0x40`) e uma RefString estática com refcount 2, mas exige mais peças que a busca de idioma.

### Telas do DR2 Hook

Versão atual: abas nativas e listas com rolagem, no molde de Opções > Gráficos. A primeira versão desta seção usava três telas `smart_hub` de 8 posições, sem rolagem e com o valor dentro do rótulo; foi substituída.

| Tela | Molde | Estado | Conteúdo |
| :--- | :--- | :--- | :--- |
| `dr2hook_hub` | `graphics_calibration` (`smart_screen_tabbed`) | `1146224640` | Só o `SBTabGroup`, sem `requested_index_check_data_path`. |
| `dr2hook_page_main` (aba "DR2 Hook") | `basic_graphics` (`smart_screen`) | nenhum | Botões literais Open overlay, Reload Lua mods e Reload native core. |
| `dr2hook_page_mods` (aba "Mods") | `basic_graphics` | nenhum | 24 linhas `button`, chave `lng_dr2hook_mod_N`, evento `dr2hook_nav_mod_N`. |
| `dr2hook_mod` | `basic_graphics` | `1146224641` | 24 linhas `toggle_15col` com `IBComboTextData` (`dr2hook_opt_N.index`, `dr2hook_opt_N.list[%u]`, `list_value_format_id="explicit"`) e `IBSelectableSimple` (`dr2hook_do_opt_N`). |

As páginas das abas usam `data_parent_override="dr2hook_hub"`: os eventos caem em `ui.dr2hook_hub.event`, que o estado do host observa. As quatro telas ficam no World de `graphics_calibration`, que as Opções da pausa já abrem. Das páginas se tira o rodapé `apply`, como em `profile_save_management`. Título (`SBScreenTitle` e o item `title`) e painel da direita (`smart_contextual_info.text_title` e `.text`) usam chaves `lng_dr2hook_*`. A lista do `SBScrollableItemFlow` segue o formato do binário: `\r\n` e 8 espaços antes de cada id, `\r\n` e 6 espaços no fim.

Dados criados pela DLL no slot `+0x80` da vtable do `StateScreenFECore` (`0x141256f18`, stub `0x1406bfe70` `c2 00 00`), trocado só para os dois estados. O Enter `0x1402d2040` chama esse slot uma vez por entrada, antes de abrir a tela. Funções e regras do data store em [UI Tabs](ui_tabs.md#especificação-para-telas-próprias).

| Estado | Nós sob `ui.<tela>` |
| :--- | :--- |
| hub | `tabs.info[0..1].screen`/`.label` (nome da página e "DR2 Hook"/"Mods"), `tabs.current_index` (Mods ao voltar da tela de um mod, senão 0), `dr2hook_slot_N` (int, visibilidade de reserva) |
| mod | `dr2hook_opt_N.list[]` (textos do combo; ao menos um, vazio, para a lista nunca ficar vazia), `dr2hook_opt_N.index` (int tipo 5), `dr2hook_vis_N` |

Eventos, no detour de `+0x88`, só nos dois estados:

| Evento | Tratamento |
| :--- | :--- |
| `dr2hook_do_overlay`, `dr2hook_do_reload_mods`, `dr2hook_do_reload_core` | DLL. Devolve verdadeiro. |
| `dr2hook_do_opt_N` (A) | Só em botão: enfileira `(mod, N, -1)`. Em toggle e choice o valor muda só pelo combo. |
| `dr2hook_nav_mod_N` | Com `N` abaixo do número de mods, guarda o mod selecionado e a aba Mods para a volta, e devolve falso para o runner seguir o link. Sem mod, consome. |
| `back` | Fluxo. |

O combo grava o índice no nó sem gerar evento. O hook do predicado `0x140d09380`, que roda na thread da UI, chama `NativeScreenTick` no máximo a cada 15 ms. Com a tela de mod aberta, ele trava o store, acha cada `ui.dr2hook_mod.dr2hook_opt_N.index` por hash (`0x140c815e0`), lê o int em `nó+0x30` e enfileira `(mod, N, índice)` quando muda. Quando a raiz some, na saída do estado, a leitura para. O hash reimplementado é conferido contra o `Path` em `estado+0x40`; se não bater, o log avisa e a leitura fica desligada.

Visibilidade: cada linha dinâmica tem `BVisibilityControlData data_path="dr2hook_slot_N"` (ou `dr2hook_vis_N`) com `watch_data="false"` e sem `glyph`. O hook do predicado esconde as linhas além do número de mods ou de opções quando `+0xa0` aponta, dentro da imagem, para `ui.dr2hook_hub.dr2hook_slot_N` ou `ui.dr2hook_mod.dr2hook_vis_N`. A linha `0` nunca some. Se o caminho não cair na imagem, vale o int que a DLL grava no nó. Quando o modelo muda, a DLL reaplica o texto do item com `ApplyText`.

Textos: a busca de idioma responde `lng_dr2hook_*`, com ou sem `_caps`, a partir do modelo publicado pelo core em `Dr2Host_NativeMenuPublish`. Linha sem mod ou sem opção fica vazia; a linha `0` mostra "No mods loaded" ou "This mod has no options".

Testado fora do jogo (`test_ui_patch` com os arquivos reais, `test_mod_menu`, `test_lua_engine`). No jogo, falta ver: a faixa de abas e LB/RB sem estado de abas, as páginas abertas por nome, `button` e `toggle_15col` fora das telas de origem, `IBComboTextData` com a lista criada pela DLL, `IBSelectableSimple` num combo, as 24 linhas com rolagem, a leitura do índice por hash e a visibilidade.

### Itens com valor

Toggle e lista usam `IBComboTextStatic data_path=... value_list="a;b"`. Nos dados, `value_list` é sempre uma lista de chaves `lng_` (48 usos). `explicit_string` não aparece em nenhum combo dos dados, mas o executável tem a string ao lado de `value_list`. Que ele torne os valores literais é HIPÓTESE. Como a busca de idioma já é nossa, chaves `lng_dr2hook_*` na `value_list` bastam. `IBComboTextData` (90 usos) tira a lista de `list_value_data_path="x.list[%u]"`, preenchida pelo C++. Ao apertar `< >`, `0x140d58a60` chama o setter `0x140d08350`: sem o nó ele devolve `0x57` e nada muda; com o nó grava e notifica os observadores, sem gerar `event`. A DLL precisa observar o nó ou lê-lo a cada quadro. CONFIRMADO no executável ([UI Tabs](ui_tabs.md#combos)). O slider usa `IBSlider`. Nos três, o valor mora num nó do modelo de dados que tem de existir: o setter de índice `0x140d08350` recusa nó ausente. Os objetos `toggle_15col` e `nub_slider_labelled_15col` aparecem só em telas `smart_screen`, e `smart_hub` só usa `smart_hub_button_5col`. PROVÁVEL que exijam trocar o molde e criar os nós pela DLL, e gravar o valor pelo setter do jogo (`0x14011eba0` e parecidos) na thread do jogo.

`SBDataDeclarator` (8 telas) declara nós do modelo de dados na própria tela: `<data data_path="selected_option_index" type="int" value="0"/>`, com os tipos `int`, `string`, `double` e `sign`. Em `replay` e `spectator` há a variante `<dataItems><data id=... type=... value=.../>`. É o candidato para criar `ui.<tela>.<caminho>` dos combos sem a DLL, de modo que o setter `0x140d08350` não recuse. HIPÓTESE, a testar. Hoje nenhum combo dos dados depende só de dados: os `data_path` são preenchidos por estados C++.

Há dois atalhos sem mexer nos dados, os dois só por análise estática:

- **Reaproveitar um link que a pausa já tem.** No detour do dispatcher, chamar `0x140215a80(estado, "options")` e devolver verdadeiro. O runner abre a tela no próximo tick, pelo mesmo caminho do item nativo.
- **Saltar para qualquer nó.** Interceptar `FindLink` (`0x140c7b180`) no nó de pausa e devolver um link próprio. O `back` do nó alcançado pode não voltar à pausa.

Montar a tela, o estado e o nó direto na memória (chamando `0x140d1f3f0` com elementos fabricados) é caro e frágil. Injetar nos dados, antes que o Coordinator e o carregador de `World` os leiam, reaproveita o caminho que o jogo já usa.

### Abas e listas com rolagem

Há dois mecanismos de abas. Nenhuma das 38 telas com abas usa um estado genérico. CONFIRMADO nos dados e no executável:

| Mecanismo | Telas | Quem define as abas |
| :--- | :--- | :--- |
| `SBTabGroup` (host `smart_screen_tabbed`, ex. `graphics_calibration`) | 35 | O behaviour é genérico: desenha a faixa, trata LB/RB e abre a página pelo nome lido em `tabs.info[i].screen`. O estado C++ só cria esses nós. Em `StateScreenGraphicsCalibration`, o CreateTabs `0x1402cecd0` carrega pares `{lng_basic_graphics_tab_label, basic_graphics}` (strings em `0x141259358`/`0x141259378`) e cria um controlador de página de `0xc0` bytes por aba. Nos dados, nada liga o host às páginas. Detalhes em [Abas no executável](ui_tabs.md). |
| `SBScreenTabControl` (host `smart_results_tabbed` ou `service_area`) | 6 | Os dados: `<tab screen_id=... title=... enable_data_path=.../>`. A página é uma `Screen` no mesmo `World`, com `glyph="results_set_library.switch"` e `data_parent_override` apontando para o host. Os `screen_id` usados, como `time_trial_results_splits`, não existem no executável. |

Atributos do `SBScreenTabControl` que o parser conhece: `max_visible_tabs`, `hide_single_tab`, `tab_transition_blocks_input`, `stacker_glyph`, `stacker_tab_glyph`, `tab_label_glyph_path`, `tab_highlight_glyph_path`, `screen_id`, `show_icon`, `button_extras_item_id`, `num_enabled_out`, `tooltip_*`, `button_l` e `button_r`. Que o comportamento troque a página e trate LB/RB sem o estado dono é HIPÓTESE, forte pelos atributos `button_l`/`button_r`. As páginas de resultado têm 12 linhas (`"0"` a `"11"`, `scroller_glyph="scroller"`).

Lista com rolagem, como em Gráficos. CONFIRMADO:

- A tela é `object="smart_screen"`. Cada linha é um `Item glyph="smart_set.N.switch"` com `IBStackItem` e `BSwitchStatic object="toggle_15col"`. A tela tem `SBItemStack glyph="smart_set.stacker"`, e o rodapé `apply` usa `smart_set.switch_footer` e `IBScrollTargetOverride fallback_direction="up"`.
- `<SBScrollableItemFlow wrapV="true" scroller_glyph="smart_set.scroller">` aparece 33 vezes, sempre assim. No binário os ids ficam um por linha, separados por CRLF, como na grade: `"\r\n        item_0\r\n        item_1 ..."` em `basic_graphics`. O `screens.xml` convertido mostra tudo numa linha, mas isso é artefato da conversão. Dos 80 textos de fluxo, 66 são listas com um id por linha e 14 são grades com colunas ([limites](ui_limits.md)).
- `advanced_graphics` usa `smart_set.0` a `.28`: 29 posições mais o rodapé. É o maior índice em `smart_screen`.
- Nenhuma `smart_screen` é aberta por `StateScreenFECore`. Que ele abra uma `smart_screen` clonada é HIPÓTESE, forte, porque o estado só abre a tela pelo nome.
- `SBDynamicItemFlow` (lista virtualizada com `top_index_data_path`, `view_size_data_path`, `list_size_data_path` e 12 linhas `view[N]`) depende do C++ para preencher.

Caminhos, do mais viável ao menos:

1. **`smart_screen` com rolagem e combos, abas por LB/RB sem faixa visual.** Clonar `basic_graphics` e usar até 29 linhas `toggle_15col` com `IBComboTextStatic`, `value_list` de chaves `lng_dr2hook_*` e `SBDataDeclarator` para os índices. A DLL lê o índice no modelo de dados. Falta verificar: se o `SBDataDeclarator` cria o nó e o combo gira, se o `StateScreenFECore` abre `smart_screen`, e se o `toggle_15col` responde sem o C++ de gráficos.
2. **Abas nativas com `SBScreenTabControl`.** Um host clonado de `time_trial_p2p_results`, com as abas "DR2 Hook" e "Mods" apontando para páginas clonadas de `time_trial_results`. Falta verificar se o comportamento troca as páginas sozinho e se `toggle_15col` funciona dentro do slot `smart_results_set`.
3. **`SBTabGroup`, igual a Gráficos.** Host clonado de `graphics_calibration` com `StateScreenFECore`, e páginas `smart_screen` com `data_parent_override` apontando para o host, para que `event` caia em `ui.<host>.event`. A DLL cria `tabs.info[i].screen`, `.label` e `tabs.current_index` num hook do slot `+0x80` (`0x141256f18`, hoje o stub `0x1406bfe70`), chamado pelo Enter `0x1402d2040` antes de abrir a tela. Mais trabalho que os outros, mas é a faixa de abas de verdade. Ver [Abas no executável](ui_tabs.md#como-seria-na-nossa-tela). É o caminho implementado, descrito em [Telas do DR2 Hook](#telas-do-dr2-hook).

### Em aberto

- **Arquivos soltos ou pacotes.** Resolvido na DLL pelo detour do parser. Não há evidência de que arquivos soltos tenham prioridade, e o `PATCH_MANAGER` com pacotes `info_*.nefs` continua só HYPOTHESIS.
- **Posições do `smart_hub`.** O `pause_menu` usa `smart_set.0` a `smart_set.10`. Não se sabe se existe `smart_set.11` na cena PSSG. Por isso o item da pausa continua sendo o `item_9`.
- **Ids.** Os ids de estado e de nó não são CRC32 dos nomes. Ainda não se sabe se o jogo exige alguma relação entre id e nome.
- **Alguns `.pssg` grandes** saem da extração alguns KB menores que o tamanho declarado. Deve haver um tipo de bloco que a ferramenta não trata. Os `.bin` e os `.xml` batem exatamente.
