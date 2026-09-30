# Menu de pausa

## Overview

A tela da captura, com o título `MENU DE PAUSA` e os itens Continuar, Reiniciar, Voltar à área de serviço, Opções e Sair para o menu principal, é o estado de fluxo `StatePauseScreen`. O texto visível não está no executável. A instância ao vivo carrega a chave `pause_menu`, e os rótulos saem de uma tabela de localização já descomprimida na memória.

Nada desta página foi escrito no processo. O índice selecionado e a lista de itens ainda não foram localizados.

## Known Structures

O registro do estado está em `0x140031b60`. Ele publica o nome `StatePauseScreen` (`0x141248f30`) junto com duas funções, `0x1402511a0` e `0x14028edc0`, por `0x1401519b0`.

`0x14028edc0` aloca `0x130` bytes e chama `0x1402c2d40`. No fim grava a vtable `0x141250b50` e zera o byte em `+0x128`. Com o menu aberto no pid `3597144`, duas instâncias tinham essa vtable: `0x15cf37b0` e `0x15cf3910`. As duas apontavam para a chave `pause_menu`.

`0x1402d2040` é o método de vtable no deslocamento `+0x18`. Ele lê o ponteiro em `+0x100`, que nessa sessão era `pause_menu`, e o passa a `0x1402e3e50` com o callback `0x1402d5ad0`.

## Memory Layout

Offsets do objeto de `0x130` bytes. Os valores são da sessão com o menu aberto e Continuar em destaque.

| Offset | Tipo | O que foi lido | Confiança |
| :--- | :--- | :--- | :--- |
| `+0x00` | ponteiro | Vtable `0x141250b50`. Dezoito ponteiros de função, do índice `0` a `17`. | CONFIRMADO |
| `+0x38` | ponteiro | `0` numa instância e `0x135003c0` na outra. O alvo tem a vtable `0x1413c9828` e os textos `backbuffer colour` e `rigidbody`. É um nó de cena. | CONFIRMADO: não é a lista de itens |
| `+0x90` | ponteiro | `0x371304e0` nas duas. Construído por `0x140348d30`. | Observado; papel UNKNOWN |
| `+0xc8` | ponteiro | `0x1411f8550` nas duas. O construtor grava esse endereço. | Observado; papel UNKNOWN |
| `+0xf8` | qword | `0xb`. É o tamanho do buffer da chave, com o terminador. | STRONG EVIDENCE |
| `+0x100` | ponteiro | Chave `pause_menu`. | CONFIRMADO |
| `+0x118` | word | `1`. O construtor grava `1`. | Observado; papel UNKNOWN |
| `+0x11c` | dword | `0xffffffff`. O construtor grava `-1`. Continuou `-1` com Continuar em destaque. | Não é o índice do destaque |
| `+0x120` | qword | Handle da tela no gerenciador de UI, `{byte, u32 hash FNV-1a do nome}`, devolvido por `0x140d32200`. Ver [UI Data](ui_data.md#do-evento-à-tela-no-executável). | CONFIRMADO por análise estática |

## Text

A frase `MENU DE PAUSA` está em UTF-8 na memória do processo, dentro de um bloco de textos de interface. Não está no `dirtrally2.exe`.

Chaves de localização presentes nesse bloco e compatíveis com os cinco itens da captura:

| Chave | Endereço nesta sessão |
| :--- | :--- |
| `lng_pause_menu_title` | `0x149c5bca` |
| `lng_pause_menu` | `0x149a1ab0` |
| `lng_pause_menu_button_continue` | `0x149ccf8b` |
| `lng_pause_menu_button_restart` | `0x149ccfaa` |
| `lng_return_to_service_area` | `0x149a1e18` |
| `lng_pause_menu_button_options` | `0x149cd0ca` |
| `lng_pause_quit_button` | `0x149a4d6f` |

`lng_pause_menu_button_continue`, `lng_pause_menu_button_restart` e `lng_pause_menu_button_options` aparecem uma vez, dentro do bloco de localização. Nenhum ponteiro alinhado do heap aponta para esses endereços. `lng_return_to_service_area` tem cópias fora do bloco, cada uma precedida pelo par `0x2f156e8` / `0x1b01`. A ligação de cada chave com o texto português e com o botão desenhado continua UNKNOWN.

Há chaves vizinhas que a captura não mostra: `lng_pause_menu_button_repair_tyre`, `lng_pause_menu_button_reset_vehicle`, `lng_pause_menu_button_rules`, `lng_pause_menu_button_replay`, `lng_pause_menu_button_retire`.

## Selected Index

CONFIRMADO fora do `StatePauseScreen`: o foco é o ponteiro em `+0x50` do objeto de navegação da tela `ui.pause_menu`, e o item em foco tem `+0x230 = 0`. Os detalhes estão em Item List. O resto desta seção registra por que o objeto `StatePauseScreen` não serve.

Dois snapshots, com Continuar em destaque e depois com Reiniciar, foram comparados byte a byte.

`+0x11c` ficou `-1` nos dois. `+0x50` ficou `2` na instância que tem filho em `+0x38`, também nos dois. Nenhum dos dois acompanha o destaque.

O que trocou de endereço foi a instância preenchida. Com Continuar, o filho `0x135003c0` estava em `0x15cf3910`. Com Reiniciar, o mesmo filho estava em `0x15cf37b0`, e a outra instância ficou com `+0x38 = 0`. Os ponteiros internos (`+0xd8`, `+0x100`, `+0x128`) seguem o endereço da própria instância.

Duas leituras seguidas, as duas com o rótulo Reiniciar, moveram `+0x118` de `1` para `0` e somaram `5` ao byte baixo de `+0x120` na instância preenchida. Isso ocorreu sem troca de item.

Na sessão `/tmp/dr2_pause/session.jsonl` a instância viva foi `0x15cf37b0` e o filho `0x135003c0`. De `0 s` a `10,26 s`, com o destaque descendo de Continuar até Opções, a tela e o filho ficaram byte a byte iguais. Em `10,26 s`, ao abrir Opções, `+0x38` foi a `0` e `+0x50` foi de `2` a `0`. Em `25,40 s`, de volta ao menu, o mesmo filho religou e `+0x50` voltou a `2`. O filho tem `1920` em `+0x1e4` e `1080` em `+0x1e8` o tempo todo em que está ligado, inclusive depois da ida à resolução. A tela de gráficos não está nessa árvore.

O ponteiro da tela viva aparece numa tabela em `0x15cf5d78`. O qword seguinte, `0x15cf5d80`, aponta para o caminho `rt_general_col/smart_scroll_group`. Isso é o slot ao lado, não um campo do `StatePauseScreen`.

## Item List

A lista vive na tela de binding `ui.pause_menu`, fora do `StatePauseScreen`. Para achá-la, parte-se da string internada `ui.pause_menu`, cujo cabeçalho fica `0x10` bytes antes do texto. O único qword que aponta para esse cabeçalho é o `+0x60` de um objeto com vtable `0x1413d62d0`, que usa o template `smart_hub`. Nesta sessão, o objeto estava em `0x47aec3e0`.

| Offset da tela | Conteúdo | Confiança |
| :--- | :--- | :--- |
| `+0x60` | cabeçalho de `ui.pause_menu` | CONFIRMADO |
| `+0x78..+0x80` | seis filhos; o sexto tem a vtable `0x1413d6858` e é a navegação | CONFIRMADO |
| `+0xa0..+0xa8` | onze pares `{nome internado item_N, item}` | CONFIRMADO |
| `+0xc0` | `0x30003` com a lista na tela e `0x30000` com Opções aberta | Não é o índice do foco; papel UNKNOWN |
| `+0xe0 + 0xe0*N` | objeto de ação do item N (vtable `0x1413d9528`); `+0xa0` guarda o nome do evento | CONFIRMADO |

Cada item tem a vtable `0x1413d8058` e ocupa `0x270` bytes. `+0x230` vale `0`, `1` ou `2`.

| N | Evento | Rótulo | `+0x230` |
| :--- | :--- | :--- | :--- |
| 0 | `continue` | `lng_button_continue` | 1 |
| 1 | `restart_race` | dinâmico | 1 |
| 2 | `objectives` | `lng_objectives_summary` | 2 |
| 3 | `recover_vehicle` | `lng_recover_vehicle` | 2 |
| 4 | `end_session` | dinâmico | 2 |
| 5 | `retire_from_event` | `lng_retire_from_event` | 2 |
| 6 | `return_to_service` | `lng_return_to_service_area` | 1 |
| 7 | `options` | `lng_options` | 0 |
| 8 | `ghost_select` | `lng_ghost_select` | 2 |
| 9 | `reset_view` | `lng_vr_reset_view` | 2 |
| 10 | `quit` | dinâmico | 1 |

`+0x230` do item: `0` = em foco, `1` = visível, `2` = oculto. CONFIRMADO. Com o `watch_pause_focus.py` rodando, o destaque foi de Opções a Continuar, voltou a Opções e passou pelos itens 7, 6, 1, 0, 1, 6 e 7, nessa ordem. O `0` acompanhou o destaque a cada passo, e os itens ocultos ficaram em `2` o tempo todo.

`+0x50` da navegação é o item em foco. CONFIRMADO na mesma sessão: ele apontou para o item destacado em cada passo. Com Opções aberta, foi a `0`. Na volta, apontou de novo para o item 7.

`+0x60` da navegação ficou no item 0 o tempo todo. `+0x68` ficou no item 7, inclusive na volta de Opções, quando o foco foi restaurado para Opções. HYPOTHESIS: `+0x60` é o foco padrão e `+0x68` o último item ativado. Nenhum outro item foi ativado nesse teste.

`+0x230` é recalculado pelo jogo. Com autorização do usuário e o menu aberto, o `+0x230` do item 9 (`reset_view`) foi gravado de `2` para `1`. A escrita retornou 4 bytes. Na leitura 50 ms depois, já estava de novo em `2`, e continuou assim em 0,5 s, 1,5 s e 3 s. Numa segunda tentativa, o valor foi regravado sempre que voltava a `2`, durante 8 s. Foram 883 escritas, e o jogo reverteu 882 delas, cerca de 110 por segundo. O estado vem de outra fonte a cada quadro. HYPOTHESIS: essa fonte é o booleano `reset_view_available` ou o `smart_set.9.switch` do item.

A fonte da visibilidade é um filho de condição do item. A análise estática mostrou o seguinte. A navegação chama o update do item a cada quadro (`0x140d5b120`), e o avaliador `0x140d5d460` percorre uma lista de predicados cuja cabeça fica em `item+0x1e8`. O predicado `0x140d09380` testa `child+0x28 == 0`. Quando o teste dá verdadeiro, o item é desabilitado e escondido: `0x140d4d39c` grava `2` em `+0x230`, e `0x140d4dc9b` grava `1`.

Leitura ao vivo da cadeia `nó = *(*(item+0x1e8))`, `impl = *(nó+0x48)`, `*(impl+8) = 0x140d09380`, `filho = *(impl+0x10)`:

| Itens | Predicados | Filho | `filho+0x28` | `+0x230` | `+0x234` |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0, 7 | nenhum | — | — | visível | `0x101` |
| 1, 6, 10 | 1 | vtable `0x1413d3100` | `1` | visível | `0x101` |
| 2, 3, 4, 5, 8, 9 | 1 | vtable `0x1413d3100` | `0` | `2` | `0x100` |

Nos onze itens, `filho+0x28` bate com a visibilidade. CONFIRMADO por leitura. Todos os filhos são da variante `0x1413d3100`, a que é ligada a um nó do modelo de dados. HYPOTHESIS: uma escrita do jogo nesse nó recalcula o byte, e ela ocorre na abertura da tela.

Com autorização do usuário e o menu aberto, `filho+0x28` do item 9 (`reset_view`) foi gravado de `0` para `1`, com uma única escrita de 1 byte. Em 50 ms, `+0x230` foi para `1` e `+0x234` para `0x101`. Os dois continuaram assim em 0,3 s, 1 s, 3 s e 6 s, e o byte não foi revertido. `+0xd0` do item mudou de `(0,355, 0,863)` para `(0,462, 0,623)`.

O usuário confirmou na tela. Apareceu uma linha nova com o texto de redefinir a exibição da RV, e o destaque chegou até ela. Selecionar essa linha não fez nada visível, o que é esperado para `reset_view` sem RV. Depois de entrar numa subtela e voltar, a linha sumiu: a ativação da tela recalcula `filho+0x28` a partir do nó do modelo de dados. CONFIRMADO: um item oculto pode ser mostrado e selecionado ao vivo com uma escrita de 1 byte, e ele vale até a próxima ativação da tela.

`+0xc0` da tela não é o índice do foco. Ele ficou em `0x30003` com o destaque em todos os itens, foi a `0x30000` quando Opções abriu e voltou a `0x30003` no retorno.

## Label

O texto de um item vem de um filho de binding estático. Ele fica no vetor `[item+0x1c0, item+0x1c8)`, tem a vtable `0x1413d2e58` e aponta `+0x18` para `text_title`. `+0x28` desse filho aponta para uma string com refcount, que guarda a chave de localização, e `+0x30` é o byte `explicit`. CONFIRMADO por leitura em três itens:

| Item | Filho | String em `+0x28` | Palavra em `+0x08` | `explicit` |
| :--- | :--- | :--- | :--- | :--- |
| 9 | `0xa8923e40` | `0xa8923e80` → `lng_vr_reset_view` | `0x1201` | `0` |
| 0 | `0xa8922a40` | `0xa8922a80` → `lng_button_continue` | `0x1401` | `0` |
| 7 | `0xa8923a00` | `0xa886b320` → `lng_options` | `0xc01` | `0` |

O layout da string é `+0x00` alocador `0x2f156e8`, `+0x08` u32 com o refcount no byte baixo e `(len+1) << 8` acima dele, e `+0x10` o texto com NUL. O setter é `0x140d36360`. Não existe deduplicação global dessas strings.

A análise estática indica o seguinte. O slot 6 da vtable do filho (`0x140d040c0`) monta um comando `SetText` (vtable `0x1413d8430`) com o ponteiro do texto, o tamanho e o `explicit`, e o põe numa fila de comandos. O renderer traduz a chave e guarda o resultado. O texto não é relido a cada quadro. HYPOTHESIS: o slot 6 roda quando a tela é ativada, e `explicit = 1` desenha o texto literal, sem tradução.

Itens com rótulo dinâmico, como o 10, usam a vtable `0x1413d2df0` e um nó de binding, por exemplo `ui.pause_menu.quit_label`. O texto traduzido de `lng_vr_reset_view`, `Redefinir exibição (RV)`, está no bloco de traduções, e nenhum ponteiro alinhado aponta para ele.

Teste ao vivo, feito com autorização do usuário. A string `0xa8923e80` tem refcount 1 e só um ponteiro aponta para ela. O texto dela foi trocado no próprio lugar por `DR2 ModLoader`, a palavra `+0x08` foi de `0x1201` para `0xe01` e `explicit` (`0xa8923e40+0x30`) foi para `1`. O usuário fechou e reabriu a pausa, e as três escritas continuaram lá, com a tela, o item e a string nos mesmos endereços. Com `filho+0x28 = 1` gravado de novo no filho de condição, a linha apareceu como `DR2 ModLoader`, com o texto literal. CONFIRMADO: `explicit = 1` desenha a string sem tradução, e o texto é reenviado quando a tela abre. A visibilidade volta a oculta a cada abertura. A troca de texto permanece. O conteúdo original foi salvo em `/tmp/dr2_pause/item9_label_backup.bin`.

## Item Count

A tela `ui.pause_menu` tem onze itens fixos, e cinco estão visíveis. Não se achou um campo que valha `5`. HYPOTHESIS: a contagem visível é derivada de `+0x230` e dos booleanos `*_available`.

## Action

A abertura da tela está fechada: `0x1402d2040` entrega a chave `pause_menu` a `0x1402e3e50` e, em seguida, chama os virtuais em `+0x80` e `+0x78`.

Um botão chega como o nome de um evento. Isto vem de análise estática e ainda não foi confirmado ao vivo.

- `0x1402d5ad0` é o callback de UI compartilhado. Ele lê o nome do evento e chama o virtual `+0x88` com esse nome. Se o virtual devolve falso, ele segue para `0x140215a80`, que copia o nome para a string em `+0xc8`. O nome vira então a próxima transição de fluxo.
- `+0x88` da vtable `0x141250b50` é `0x140285640`. Ele compara o nome com `strcmp` contra `restart_race` (`0x141250c48`), `retire_from_event` (`0x141250cd0`), `end_session` (`0x141250ce8`), `quit` (`0x141242224`) e `reset_view` (`0x141250d18`). `quit` pode virar `quit_retire` (`0x141250d08`). Devolve verdadeiro quando trata o evento.
- `+0x80` é `0x14025b460`. Ele grava no modelo de dados os booleanos de visibilidade: `restart_available`, `objectives_available`, `recover_available`, `end_session_available`, `retire_event_available`, `return_to_service_available`, `quit_available`, `ghost_select_available` e `reset_view_available`.
- As strings `pause_menu`, `ui.pause`, `lng_pause_menu*` e `repair_tyre` não estão no executável. A lista de botões e os rótulos vêm dos dados. Isso é HYPOTHESIS forte.

As entradas `+0x18`, `+0x80` e `+0x88` e as seis strings de evento foram conferidas no arquivo.

## Initialization

`0x1402c2d40` zera a chave, põe `+0x118` em `1` e `+0x11c` em `-1`. `0x14028edc0` é quem cria o objeto de `0x130` bytes quando o fluxo entra em `StatePauseScreen`.

## Candidate Injection Point

Candidato para a ativação: trocar a entrada `+0x88` da vtable `0x141250b50` por uma função que trate um nome de evento próprio, devolva verdadeiro e repasse o resto a `0x140285640`. O nome próprio tem de devolver verdadeiro para não ficar pendente como transição. Pela análise estática, um nome sem link em nenhum nó da pilha não abre nada: ele só fica em `+0xd8` até o próximo evento ou até a saída do estado. HIPÓTESE forte, não testada ao vivo. `0x1402d5ad0` e `0x140215a80` são compartilhados por outras telas e não servem para esse hook.

Candidato para a visibilidade: reaproveitar o item 9 (`reset_view`). Mostrá-lo já foi confirmado ao vivo, com `filho+0x28 = 1`. Como a ativação da tela recalcula esse byte, a DLL precisa regravá-lo a cada abertura. Os pontos para isso são um pós-hook em `+0x80` (`0x14025b460`) ou no avaliador `0x140d09380`. Faltam o texto (hoje `lng_vr_reset_view`) e a ação: o nome `reset_view` fica em `+0xa0` do objeto de ação e é tratado por `0x140285640`.

## Implementação

`src/core/pause_menu.cpp` fica na `dxgi.dll` residente. Assim, os hooks continuam válidos depois de um recarregamento do core com F8. `InstallPauseMenuHooks()` roda logo depois de `InitializeHooks()`. Antes de instalar qualquer hook, ele confere os bytes iniciais das três funções e se `+0x88` da vtable `0x141250b50` aponta para `0x140285640`. Se algo não bater, registra no log e não instala nada.

| Hook | Função | O que faz |
| :--- | :--- | :--- |
| Predicado | `0x140d09380` `bool (condição*)` | Quando a condição tem a vtable `0x1413d3100` e `+0xa0` aponta para `ui.pause_menu.reset_view_available`, devolve falso e o item aparece. O texto só é comparado quando o ponteiro cai dentro da imagem do executável. O hook guarda o item (`+0x10`) e se o jogo queria escondê-lo. |
| Texto | `0x140d040c0` `void (binding de texto*)` | Antes do original, quando o binding estático pertence a esse item e a string ainda é `lng_vr_reset_view` com refcount 1, troca o texto no próprio buffer por `DR2 ModLoader` e liga `explicit`. Ponteiros alheios passam por `VirtualQuery` antes de serem lidos. |
| Evento | `0x140285640` `bool (tela*, const char* evento)` | Com o item sequestrado, `reset_view` devolve verdadeiro sem chamar o original e marca um pedido. Os outros eventos vão para o original. |

O core lê o pedido a cada quadro por `Dr2Host_ConsumePauseMenuRequest` e abre o overlay. Com RV ativa, o jogo mostra o item por conta própria, e o evento `reset_view` segue para o original. Nesse caso o rótulo continua `DR2 ModLoader`. HYPOTHESIS, não testado com RV.

A primeira versão fechou o jogo no carregamento da especial, com violação de acesso em `ucrtbase.dll + 0x6fe60`, o `strcmp` da DLL. Ela chamava `strcmp` sobre `+0xa0` de qualquer condição com essa vtable e sobre `+0x18` de qualquer binding de texto. Nem todas as instâncias guardam ali um texto válido. A segunda versão não fechou o jogo, mas o item não apareceu. Ela comparava `+0xa0` com o endereço fixo `0x14176ad38`, e esse endereço muda. Os caminhos ficam num pool que o jogo monta em runtime dentro do `.data` da imagem. Na sessão seguinte, `ui.pause_menu.reset_view_available` estava em `0x141776269`, e `0x14176ad38` guardava `invite.friend[6].text_player`. A terceira versão aceita qualquer ponteiro dentro da imagem, compara o texto e guarda em cache o ponteiro que bater. CONFIRMADO no jogo: o item aparece como "DR2 ModLoader" no menu de pausa, sem crash ao carregar a especial. Selecioná-lo abre o overlay, e item e rótulo continuam lá depois de reabrir a pausa e de entrar e sair de Opções.

## Roadmap

1. Item "DR2 ModLoader" no lugar do item 9. Implementado em `src/core/pause_menu.cpp` e validado no jogo.
2. Tela totalmente personalizada, aberta por esse item. `options` é um evento que o dispatcher `0x140285640` não trata, então vira uma transição de fluxo e abre `ui.options_ingame`, outra tela `smart_hub`. As telas, os estados e esse link estão nos dados do jogo, descritos em [UI Data](ui_data.md). A cadeia no executável e o ponto de leitura dos dados estão no mesmo documento. A versão atual altera os dados no boot: o item 9 passa a vir dos dados, com rótulo literal e sem condição de visibilidade, e abre a tela `dr2modloader`. CONFIRMADO no jogo: o item abre a tela nativa em vez do overlay. Os hooks desta página continuam instalados como reserva, para o caso de o patch dos dados falhar ou estar desligado.
3. Botões da tela nativa: Open overlay, Reload Lua mods, Reload native core e Back. Os eventos são tratados pela DLL no slot `+0x88` do `StateScreenFECore`. CONFIRMADO no jogo: cursor, "Open overlay" e o título pela busca de idioma. Detalhes em [UI Data](ui_data.md#eventos-da-tela).
4. Item e título renomeados para "DR2 Hook", com as opções declaradas pela API `Menu` do Lua ([guia](../MODDING_GUIDE.md)). A primeira versão tinha três telas `smart_hub` de 8 posições.
5. Abas nativas "DR2 Hook" e "Mods" (LB/RB), como em Opções > Gráficos, listas com rolagem de até 24 linhas e combos `< valor >` na tela de cada mod. A DLL cria os dados das abas e dos combos no Enter do estado. Implementado e testado fora do jogo; falta validar no jogo. Detalhes e pendências em [UI Data](ui_data.md#telas-do-dr2-hook), [UI Tabs](ui_tabs.md) e [UI Limits](ui_limits.md).

## Gráficos

Com a página de gráficos aberta, as duas instâncias de `StatePauseScreen` ficam com `+0x38 = 0` e `+0x50 = 0`. Os controles não estão nesse objeto.

Eles aparecem noutro heap. Cada combo tem a vtable `0x1413d8f88`. O ponteiro em `-0x18` aponta para o cabeçalho de uma string internada: o texto começa em `+0x10` desse cabeçalho, e `+0x00` do cabeçalho é `0x2f156e8`. Nesta sessão o heap das strings era `0xa88d0000` e os combos começavam em `0xa86c3cc0`.

Caminhos lidos com a página aberta, e o dword em `+0x28` do combo:

| Caminho | `+0x28` |
| :--- | :--- |
| `ui.basic_graphics.resolution.list[%u]` | `19` |
| `ui.basic_graphics.aspect_ratio.list[%u]` | `3` |
| `ui.basic_graphics.refreshRate.list[%u]` | `3` |
| `ui.basic_graphics.multisampling.list[%u]` | `3` |
| `ui.basic_graphics.anisotropic.list[%u]` | `3` |
| `ui.advanced_graphics.preset.list[%u]` | `19` |

Os combos de `ui.advanced_graphics` também estavam montados. `+0x20` era `31` em todos.

Em `/tmp/dr2_pause/gfx.jsonl` a resolução ficou em `19` o tempo todo. Proporção, taxa de atualização, multisampling, anisotrópico e preset ficaram em `3`, `3`, `3`, `3` e `19`. Aos `9,45 s`, quando a lista de pausa soltou, `shader_detail`, `texture_detail` e `reflections` foram de `0` para `3`. As outras listas avançadas continuaram em `0` durante a troca de aba e a ida à página de áudio. Aos `35,16 s` a lista de pausa voltou e esses dwords não voltaram atrás. `+0x28` não acompanha o destaque nem a aba. O papel dele continua UNKNOWN.

`scripts/watch_pause_focus.py` reencontra a tela `ui.pause_menu`, os itens e a navegação a cada execução, e imprime o foco, `+0xc0` da tela e o `+0x230` de cada item quando algum deles muda.

`scripts/observe_pause_menu.py` grava uma leitura das instâncias. `scripts/record_pause_menu.py` grava quem aponta para a tela de pausa, as cópias dos rótulos fora do bloco de localização, e o dword `+0x28` de cada combo de gráficos. Os dois só leem.
