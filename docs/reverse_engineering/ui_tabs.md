# Abas, listas com rolagem e combos

Como a tela Opções > Gráficos monta as abas (LB/RB), a lista com rolagem e os combos `< > Valor`, e o que uma tela nossa com `StateScreenFECore` precisaria para ter o mesmo. Análise estática do `dirtrally2.exe`, conferida por leitura do processo com a aba Gráficos avançados aberta. Nada foi escrito no processo.

Complementa [UI Data](ui_data.md) e [Menu](menu.md).

## Resumo

A mecânica das abas é dividida em duas partes:

- **UI, genérica.** O behaviour `SBTabGroup` desenha a faixa de abas, trata LB/RB, grava `tabs.current_index` e instancia a tela da aba pelo nome lido em `tabs.info[i].screen`. Não sabe nada de estados.
- **Estado, C++ fixo.** As classes `StateScreen*Tabbed*`/`GraphicsCalibration`/`TabsSettings` só preenchem `tabs.info[i].{screen,label}` e `tabs.current_index` no modelo de dados, inscrevem o callback de evento da tela da aba e mantêm um objeto de página C++ por aba. A lista de abas está no código, não nos dados.

Uma tela com `StateScreenFECore` pode ter abas se a DLL criar esses nós antes da ativação da tela e se as páginas usarem `data_parent_override`. Só com dados, não: ninguém cria os nós.

## Estados com abas

| Classe | Registro | Criação | Tamanho | Vtable | CreateTabs (`+0x90`) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `StateScreenFECore` (referência) | `0x140039950` | `0x1402da320` | `0x128` | `0x141256e98` | — |
| `StateScreenGraphicsCalibration` | `0x140039cd0` | `0x1402da6b0` | `0x430` | `0x1412592b8` | `0x1402cecd0` |
| `StateScreenTabsSettings` | `0x140039e50` | `0x1402da8d0` | `0x450` | `0x141259500` | `0x1402cef50` |
| `StateScreenTabbedCustomConfig` | `0x140039dd0` | `0x1402da830` | `0x420` | `0x1412596a8` | `0x1402cee30` |
| `StateScreenTabbedProfile` | `0x1400343e0` | `0x140292340` | `0x420` | `0x141254728` | `0x140267dd0` |
| `StateScreenTabbedEditDevice` | `0x1400418d0` | `0x14036c390` | `0x430` | `0x1412638d0` | `0x14035a350` |

CONFIRMADO. Todos chamam o construtor base de abas `0x1402c2b90`, que chama o base comum `0x1402c2d40` e grava a vtable base `0x141258b58`.

### Construtor base de abas `0x1402c2b90`

- `+0x128`: `TabController` embutido, inicializado por `0x1403493d0(ctrl, estado, fnA, fnB, fnC)`. `fnA` → `+0x08` (impl em `+0x40`) chama `0x1402cc460`; `fnB` → `+0x48` vazio; `fnC` → `+0x88` (impl em `+0xc0`) chama `0x1402e4940`.
- `+0x290`: vetor de abas, buffer inline em `+0x2b0`, capacidade fixa `8` em `+0x298`, contagem em `+0x2a0`. Entrada de `0x28` bytes: `{char* screen, char* label, bool habilitada, shared_ptr<página>}`.
- `+0x3f0`: página ativa (shared_ptr).
- `+0x400..`: shared_ptrs das páginas, preenchidos pela classe concreta.

### Vtable base `0x141258b58` contra `StateScreenFECore`

| Slot | FECore | Base de abas | Papel |
| :--- | :--- | :--- | :--- |
| `+0x18` | `0x1402d2040` | `0x1402d1dc0` | Enter: chama o `+0x18` de cada página e salta para `0x1402d2040` |
| `+0x80` | `0x1406bfe70` (`c2 00 00`) | `0x1402cbef0` | Popular dados: `0x1402d8c50(estado, 0)` |
| `+0x88` | `0x140ebb300` (`32 c0 c3`) | `0x1402d7c30` | Evento: repassa ao `+0x90` da página em `+0x3f0` |
| `+0x90` | `0x1402d4880` | `0x1406bfe70` | CreateTabs, sobrescrito pela classe concreta |
| `+0x98` | `0x1402c4a80` | `0x1406bfe70` | Gancho de troca de aba (vazio na base) |

### CreateTabs

`0x1402cecd0` (gráficos), para cada aba: `rótulo = 0x140559f20("lng_..._tab_label")` (a busca de idioma, a mesma do hook `0x1403a8820`), e depois `AddTab<Página>(estado, &saída, screen_name, rótulo)`. As três funções `AddTab` (`0x1402ba770`, `0x1402ba350`, `0x1402bada0`) só diferem na classe de página. Cada uma aloca `0xc0` bytes, constrói a página (ex.: `0x1402efb40(página, estado+0x238, estado+0x88)`, vtable `0x14125b558`) e empilha a entrada. Se o vetor estiver cheio (8), registra o erro `0xc`.

| Estado | Abas (tela) |
| :--- | :--- |
| Gráficos | `basic_graphics`, `advanced_graphics`, `gamma_calibration` |
| Configurações | `difficulty_settings`, `assist_settings`, `preference_settings`, `osd_settings`, `vrcomfort_settings` |
| Config. personalizada | `difficulty_settings`, `championship_settings` |
| Perfil | `profile_settings`, `profile_save_management` |
| Editar dispositivo | `input_bindings`, `advanced_input`, `vibration_feedback` |

CONFIRMADO: a lista é fixa no C++; `states.bin` não tem filhos de aba.

Página: base `0x1402c1a70`, vtable base `0x1412402b8`. `+0x88` é puro (`0x140ec9914`), `+0x90` (evento) é o stub `0x140ebb300`. É um mini-estado com lógica própria (ex.: a página de gráficos preenche `ui.basic_graphics.*`).

### TabController

| Função | Assinatura | Papel |
| :--- | :--- | :--- |
| `0x1402d8c50` | `void (estado*, u32 índiceInicial)` | Copia as entradas habilitadas (`{screen, label, bool}`, `0x18`) e chama o Setup com prefixo `""` |
| `0x140369320` | `void (ctrl*, vec<desc>*, u32 índice, const char* prefixo)` | Setup: grava os nós, inscreve os callbacks, ativa a aba |
| `0x140372230` | callback | `current_index` mudou: lê o int e chama `ActivateTab` |
| `0x140370c20` | callback | `requested_index` mudou: pergunta à página (`0x1402e4940`, aviso `warn_lose_unsaved_changes`) |
| `0x1403788c0` | `void (ctrl*, u32 índice)` | ActivateTab: lê `tabs.info[i].screen`, inscreve os eventos da tela da aba (`0x1402e42f0`), grava `ctrl+0x160 = i` e chama `fnA` |
| `0x1402cc460` | `void (estado*, const char* screen)` | OnTabChanged: sai da página atual (`+0x50`), acha a entrada pelo nome, troca `+0x3f0`, chama `+0x88` e `+0x30` da página nova |
| `0x1402e42f0` | `bool (estado*, holder*, const char* screen)` | `0x1402e3e50(holder, screen, {0x1402d5ad0, estado})`: inscreve `ui.<screen>.event` no callback do estado |

Nós gravados pelo Setup, relativos à raiz da tela (`estado+0x40`, store em `estado+0x38`):

```text
ui.<tela>.tabs.info[i].screen   string (tipo 0xe)
ui.<tela>.tabs.info[i].label    string, já traduzido
ui.<tela>.tabs.current_index    int (tipo 5)
ui.<tela>.tabs.requested_index  observado se fnC existe
```

CONFIRMADO ao vivo (estado `0x159e01b0`, id `1488128951`): 3 entradas, rótulos "Gráficos básicos", "Gráficos avançados", "Calibração de gama"; `ctrl+0x160 = 1`; `+0x3f0` = página de avançados (vtable `0x141257d38`). No store, `tabs.current_index` = `1`, tipo `5`, e `tabs.info[0..2]` têm os filhos `label` e `screen`.

## SBTabGroup (UI)

Registro `0x140023e10`, fábrica `0x1401b91a0` (`0x708` bytes, construtor `0x1401a8a70`), vtable `0x141237950`, loader `0x1401b1a80`.

| Offset | Conteúdo |
| :--- | :--- |
| `+0x10` | Tela dona |
| `+0x298`, `+0x3a8`, `+0x4b8` | Três botões internos; callbacks `0x1401c2a70`, `0x1401b7f70`, `0x1401bd190` (ativação `0x1401bb520`) |
| `+0x698` | Binding `current_index` |
| `+0x6a8` | Binding `tab_info` (obrigatório) |
| `+0x6b8` | Binding `requested_index_check` (opcional) |
| `+0x6c8` | Número de abas, de `0x140d02570` → `0x140c81510(store, array, &n)` |
| `+0x6d0` | Handle da tela da aba |
| `+0x6d8`, `+0x6dc` | Índice atual e pedido |

- LB e RB são as ações `0x10` (`LeftShoulder`) e `0x11` (`RightShoulder`) da tabela `0x1415ba180`. O `SBTabGroup` as trata sozinho: não passa pelo evento `event` nem pelo estado.
- No Update (`0x1401c5d70`), com pedido diferente do atual: sem `requested_index_check`, aceita direto; com ele, grava o pedido e espera a resposta. Depois grava o índice em `current_index` (`0x140d088a0`) e chama `0x1401ba940`.
- `0x1401ba940` lê `tab_info[atual].screen` e chama o gerenciador de UI `0x140d32960`, que instancia a tela da aba dentro da tela com abas.

CONFIRMADO ao vivo: `SBTabGroup` `0x15964db0` da tela `ui.graphics_calibration` com `+0x6c8 = 3` e `+0x6d8 = 1`. Há 35 instâncias, uma por `SBTabGroup` de `screens.bin`.

Quem exibe a página é o `SBTabGroup`. O estado só inscreve os eventos da página e roda a lógica dela.

## Raiz de dados de uma tela

O carregador de telas `0x140d24220` grava em `tela+0x60` o caminho `"ui.%s"` com `data_parent_override` ou, sem ele, o `id`. CONFIRMADO. Uma página com `data_parent_override="<mãe>"` grava `event` e os `data_path` em `ui.<mãe>.*`. O jogo já faz isso: as abas de `challenge_select` são telas com `data_parent_override="challenge_select"`.

## Lista com rolagem

`SBScrollableItemFlow` (registro `0x1400969a0`, fábrica `0x140d31470`, construtor `0x140d10ac0`) grava primeiro a vtable da navegação de grade (`0x1413d6858`) e depois `0x1413d6af8`. É behaviour de tela, independente do estado. `advanced_graphics` rola 28 itens mais o botão Aplicar com `smart_set.0` a `.28` do molde `smart_screen`. O teto de posições do molde no PSSG não foi medido (HIPÓTESE: o limite vem dos glyphs `smart_set.N`, não do código).

## Combos

| Behaviour | Fábrica | Construtor | Vtable |
| :--- | :--- | :--- | :--- |
| `IBComboTextStatic` | `0x140d54ac0` (`0x368`) | `0x140d3ebc0` | `0x1413d8d88` |
| `IBComboTextData` | `0x140d54a20` (`0x378`) | `0x140d3eaa0` | `0x1413d8bf8` |

Base comum `0x140d3e880`. O loader base `0x140d48f20` lê `text_glyph`, `list_value`, `watch_data_list`, `show_steps`. O do estático, `0x140d491d0`, acrescenta `explicit_string` (bool) e `value_list` (separado por `;`). Nenhuma tela usa `explicit_string`.

- `+0x88` da vtable, `0x140d58a60(combo, u32 novo, bool notificar)`: se `novo` difere de `+0x80` (índice atual), chama `0x140d08350(combo+0x30, novo)`; com sucesso e mudança, chama `+0x90` (vazio, `0x1406bfe70`).
- `0x140d08350(binding*, u32)`: store global `[0x142016ca0]`, caminho internado em `[binding+8]+0x40`. Sem nó, devolve `0x57` e avisa `[0x142016cb0]` (nulo nesta sessão). Com nó, grava por `0x140cb2d20`, que notifica os observadores.

A mudança de valor não gera `event`. Para a DLL saber, há duas vias: observar o nó (`0x140c826d0` + `0x140c83d20`, como o TabController) ou ler o nó a cada quadro.

## API do data store

Store global em `[0x142016ca0]` (`0x135203c0` nesta sessão), o mesmo de `estado+0x38` depois do Enter. Mutex em `store+0x98`; mapa hash `store+0x118` (buckets), `+0x130` (máscara), `+0x100` (sentinela); entrada `{next, prev, chave, nó*}`. `Path` tem `0x18` bytes: `{u64 hash, 0, 2}`.

| Função | Assinatura (inferida dos chamadores) |
| :--- | :--- |
| `0x140c7e320` | `Path* (Path*, const char*)` |
| `0x140c7e340` | `Path* (Path*)` vazio |
| `0x140c7e2d0` | `Path* (Path* dst, const Path* src)` |
| `0x140c81e70` / `0x140c83fc0` | `lock/unlock (store*, store+0x98)` |
| `0x140c7fd70` | `int (store*, const Path* pai, const Path* nome, Path* saída)` filho/contêiner |
| `0x140c7fee0` | `int (store*, const Path* pai, const Path* nome, Path* saída)` array |
| `0x140c80070` | `int (store*, const Path* array, Path* saídaElemento)` |
| `0x140c80850` | `int (store*, const Path* pai_ou_0, const Path* nome, Path* saída, 0)` |
| `0x14011eba0` | `int (store*, const Path*, const char*)` string |
| `0x1401ea8a0` | `int (store*, const Path*, const int*)` int |
| `0x140c815e0` | `nó* (store*, u64 hash)` |
| `0x140c81510` | `int (store*, const Path* array, u64* n)` |
| `0x140c826d0` | `int (store*, const {fn, ctx}*, u16* handle)` |
| `0x140c83d20` | `int (store*, const Path*, u16* handle, u8 flags=0x80, 0)` |
| `0x140c83fd0` | desinscrição, usada no fechamento do holder (`0x1402ce840`) |

Nó: `+0x00` pai, `+0x08` 1º filho, `+0x20` hash, `+0x30` valor, `+0x38` tipo (`5` int, `0xe` string, `0xff` contêiner). String: `+0x30` → `{u64 1, u64 len+1, texto}`.

## Como seria na nossa tela

1. `screens.bin`: `dr2hook_hub` com `object="smart_screen_tabbed"` e `SBTabGroup` copiado de `graphics_calibration` (sem `requested_index_check`); páginas `smart_screen` (molde de `advanced_graphics`) com `data_parent_override="dr2hook_hub"`, itens `toggle_15col` + `IBComboTextStatic`, `SBScrollableItemFlow`.
2. DLL: trocar o slot `+0x80` da vtable `0x141256e98` (endereço `0x141256f18`, valor `0x1406bfe70`, bytes `c2 00 00`) só para os ids `0x4452000x`. O Enter `0x1402d2040` chama `+0x80` depois de inscrever `event` (`0x1402e3e50`) e antes de abrir a tela (`0x140d32200`), o mesmo momento em que os estados de abas gravam `tabs.*`.
3. No hook: reproduzir o Setup (`0x140c7fd70` "tabs", `0x140c7fee0` "info", `0x140c80070` por aba, `0x140c80850` + `0x14011eba0` para `screen`/`label`, `0x140c80850` + `0x1401ea8a0` para `current_index` e para cada combo).
4. Eventos: as páginas gravam em `ui.dr2hook_hub.event`, que o `StateScreenFECore` já observa. O detour de `+0x88` continua valendo.
5. Valores: observar os nós dos combos ou lê-los no `Present`.

PROVÁVEL, não testado no jogo. Riscos: criar nós fora do lock; nós que sobram quando a tela fecha e o Enter repetido (o Setup original limpa com `0x14031c900`/`0x140359b10`); `tabs.info` tem de ser array (a contagem vem de `0x140c81510`); os callbacks inscritos precisam ser desinscritos na saída.

## Especificação para telas próprias

Base: disassembly de `dirtrally2.exe` (capstone) e leitura somente de `/proc/<pid>/mem` com o jogo aberto e o estado `dr2hook_hub` ativo. Marcas: **CONFIRMADO** (código e/ou memória), **PROVÁVEL** (código lido, sem teste no jogo), **HIPÓTESE**.

### Decisão

| Parte | Via | Por quê |
| :--- | :--- | :--- |
| `tabs.info[]`, `tabs.current_index` | DLL no `+0x80` (via B) | O `SBDataDeclarator` anexa dois elementos por `[]` e não os remove ao fechar. A via B roda uma vez por Enter. |
| Índices dos combos | DLL no `+0x80` | O combo não cria o nó. Sem nó, ele mostra o índice 0 e ignora a troca. |
| Textos fixos das páginas | XML (`BTextStatic`, `value_list`) | Não precisa de dados. |
| Textos variáveis (painel, listas) | DLL grava; XML lê com `BTextData`/`IBComboTextData` | Mesmo lock e mesmos setters. |
| Eventos | `data_parent_override="dr2hook_hub"` nas páginas | Tudo cai em `ui.dr2hook_hub.event`, que o FECore já observa. |

### A. `SBDataDeclarator`

| Item | Valor | Marca |
| :--- | :--- | :--- |
| Registro | `0x140096820`, nome em `0x1413d46c0` | CONFIRMADO |
| Fábrica | `0x140d312c0`: `0x58` bytes, vtable `0x1413d6570`; vetor de entradas em `+0x38/+0x40/+0x48` | CONFIRMADO |
| Loader (`+0x28`) | `0x140d22e90`. Lê os filhos `<data>` na ordem do XML. `type` ausente dá `0x57`; tipo desconhecido dá `0x16`. | CONFIRMADO |
| Tipos | `int` `0x1413d64d0`, `double` `0x1413d6670`, `bool` `0x1413d6698`, `string` `0x1413d6520`, `sign` `0x1413d64f8`, `controller_icon` `0x1413d6548` | CONFIRMADO |
| Atributos | `data_path`, `format_id`, `value` (int via `0x14087a7d0`; string vazia por padrão) | CONFIRMADO |
| Forma `<dataItems><data id=…>` (`replay`) | O loader não lê isso. É inerte. | PROVÁVEL |
| Ativação (`+0x58` = `0x140d330c0`) | Na abertura da tela, grava `value` em cada entrada. Sobrescreve sempre. | CONFIRMADO |
| Fechamento (`+0x48` = `0x140d19a80`) | Remove o nó de cada entrada (`0x140cfcec0` → `0x140c82c50`). | CONFIRMADO |
| Raiz | `ui.<id>` ou `ui.<data_parent_override>`. Caminho começando com `ui.` é absoluto. | CONFIRMADO |
| Nó existente | Reaproveitado. O setter falha com erro 5 se o tipo for outro (exceto `0xff`, "sem valor"). | CONFIRMADO |
| `a.b.c` | Cria os segmentos que faltam. | CONFIRMADO |
| `x[n]` | Só funciona se o array existe e `n < contagem` (senão `0x16`/`0x22`). | CONFIRMADO |
| `x[]` | O setter resolve o caminho duas vezes (`0x140c7fdf0` e depois `0x140c80850`), então **anexa 2 elementos**: o 1º fica sem valor e o 2º recebe o valor. | CONFIRMADO (código) |
| Remoção de `x[]` | Falha (o hash de `[]` dá `0x2f`). O elemento fica. | CONFIRMADO (código) |
| Setter `int` | `0x1401ea790` grava o **tipo 6**. `0x1401ea8a0` grava o tipo 5. | CONFIRMADO |

Ordem de abertura (`0x140d329a0`):

1. `0x140d2d4b0`: `+0x58` de cada behaviour (os declarators gravam aqui).
2. `0x140d53d00` para cada item (os combos leem o índice aqui).
3. `+0x30` de cada behaviour (`SBTabGroup` `0x1401b78a0` passa a observar `current_index`).
4. `0x140d32cc0`: `+0x60` de cada behaviour (`SBTabGroup` `0x1401bb520` lê a contagem e o índice, cria as abas e abre a página).

Os declarators vêm antes do `SBTabGroup`: CONFIRMADO. Mesmo assim, a via A é frágil para `tabs.info`. Se a tela reabrir dentro do mesmo estado, os elementos de `[]` sobram e cada reabertura soma mais dois. Use `SBDataDeclarator` só para escalares, com caminhos sem `[]`.

### B. Criação pela DLL no hook de `+0x80`

Enter `0x1402d2040(estado, anterior)` (CONFIRMADO):

1. `0x1402e3e50(estado+0x38, estado+0x100, {0x1402d5ad0, estado})` cria `ui.<tela>`, inscreve `event` e devolve `true` só se o holder era novo.
2. Se `true`: `vtbl[+0x80](estado)`.
3. `0x1402768c0`, `vtbl[+0x78]`, e depois `0x140d32200` abre a tela.

Então o `+0x80` roda uma vez por entrada no estado, antes da tela existir.

Holder (CONFIRMADO ao vivo, `dr2hook_hub`):

| Offset do estado | Conteúdo |
| :--- | :--- |
| `+0x38` | `store*` (igual a `[0x142016ca0]`) |
| `+0x40` | `Path` resolvido de `ui.<tela>`: `{hash, 0, 2}`. O hash bate com o algoritmo abaixo. |
| `+0x58` / `+0x5a` | handles `u16` do callback e da inscrição |
| `+0x100` | `const char*` do nome da tela |

Na saída do estado, `0x1402ce840` remove `ui.<tela>` inteiro (`0x140c82c50`). Nada que a DLL criar sob essa raiz fica para trás: CONFIRMADO.

API (CONFIRMADO por disassembly):

| Função | Assinatura | Trava sozinha? |
| :--- | :--- | :--- |
| `0x140c7e320` | `Path* (Path*, const char*)` | não usa store |
| `0x140c7e340` | `Path* (Path*)` vazio | — |
| `0x140c7fd70` | `int (store*, const Path* pai, const Path* nome, Path* out)` encontra/cria | sim |
| `0x140c7fee0` | `int (store*, const Path* pai, const Path* nome, Path* out)` cria array | sim |
| `0x140c80070` | `int (store*, const Path* array, Path* outElem)` anexa **um** elemento (usa `"[]"` de `0x1412974dc`) | sim |
| `0x140c80850` | `int (store*, const Path* pai_ou_0, const Path* nome, Path* out, node** outNo_ou_0)` | **não** |
| `0x140c81e70` | `void (store*, u32* store+0x98)` trava | — |
| `0x140c83fc0` | `void (store*, u32* store+0x98)` destrava (`mov dword [rdx],0`) | — |
| `0x14011eba0` | `int (store*, const Path*, const char*)` string, tipo `0xe` | **não** |
| `0x1401ea8a0` | `int (store*, const Path*, const int32_t*)` int, tipo 5 | **não** |
| `0x140c815e0` | `node* (store*, u64 hash)` | **não** |
| `0x140c81510` | `int (store*, const Path* array, u64* n)` | sim |
| `0x140c82c50` | `int (store*, const Path*)` remove a subárvore | sim |

Regras:

- O spinlock é `lock bts [store+0x98],0`. **Não é recursivo.** Nunca chame uma função que "trava sozinha" com o lock na mão.
- Os setters só usam `Path[0]` (hash). Aceitam nó novo (`0xff`) ou do mesmo tipo; senão devolvem 5.
- Os setters notificam os observadores (`0x140c82000`) com o lock na mão. Se o despacho é imediato ou adiado: HIPÓTESE. Não mexa no store dentro de um callback de dados.

Hash de caminho (`0x140c817c0`, CONFIRMADO ao vivo):

```c
#define FNV_OFF   0xcbf29ce484222325ull
#define FNV_PRIME 0x100000001b3ull
static uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = p; while (n--) { h ^= *b++; h *= FNV_PRIME; } return h;
}
/* segmentos separados por '.' e '[' */
static uint64_t seg_hash(const char* s, size_t n, int is_index) {
    if (is_index) { char t[32]; int k = sprintf(t, "{0x%llx}", strtoull(s, 0, 10)); return fnv(FNV_OFF, t, k); }
    if (n > 3 && s[0] == '{' && s[1] == '0' && s[2] == 'x') return strtoull(s + 3, 0, 16);
    return fnv(FNV_OFF, s, n);
}
/* acc = h0; para cada seguinte: acc = fnv(acc, &h, 8) (8 bytes LE) */
```

`[]` sem número não tem hash (erro `0x2f`). Para ler um nó existente, basta `0x140c815e0(store, path_hash(...))` com o lock.

### C. Combos

| Item | Valor | Marca |
| :--- | :--- | :--- |
| GetIndex (`+0x80`) | `0x140d523e0` lê o nó `data_path` a cada chamada. Sem nó, dá 0. Não há campo de índice. | CONFIRMADO |
| Conversão na leitura | `0x140cf1990` aceita qualquer tipo numérico `0..0xa` (5 e 6 servem). | CONFIRMADO |
| SetIndex (`+0x88`) | `0x140d58a60` → `0x140d08350`: grava convertendo para o tipo que o nó já tem. Sem nó, dá `0x57` e nada muda. | CONFIRMADO |
| Ativação (`+0x30` = `0x140d529f0`) | `idx = GetIndex`, `+0x48 = idx`, `SetIndex(idx,1)`, `0x140d5cf70` desenha o texto. | CONFIRMADO |
| Texto | `0x140d5cf70` pede `+0xa8(i, &explicito)` e envia `SetText`. Sem explícito, traduz como o `BTextStatic` (busca `0x1403a8820`). Fora do intervalo, texto vazio. | CONFIRMADO / tradução PROVÁVEL-forte |
| `IBComboTextStatic` | Loader `0x140d491d0`: `value_list` dividido por `;`; `explicit_string="true"` mostra literal. Contagem = número de itens. | CONFIRMADO |
| `IBComboTextData` | Loader `0x140d49010`: `list_value_data_path` precisa de `[%u]`. Na ativação (`0x140d52ce0`), lê `n` = contagem do array e cada texto já formatado (explícito = 1). `watch_data_list="true"` observa a lista (`0x140d545e0`). | CONFIRMADO |

Toggle: `IBComboTextStatic value_list="lng_dr2hook_off;lng_dr2hook_on"` sem `explicit_string`. As chaves passam pelo hook de idioma `0x1403a8820`. O nó do índice precisa existir antes da abertura: crie-o no `+0x80`.

### D. Linhas de `smart_screen` (XML real)

Botão (`profile_save_management`, `team_offer_select`), CONFIRMADO:

```xml
<Item id="item_3" glyph="smart_set.3.switch" enable_cursor="true">
  <IBStackItem/>
  <BSwitchStatic object="button"/>
  <BVisibilityControlStatic glyph="save_dot" visible="false"/>
  <BTextStatic string="lng_reset_racenet_label" glyph="text_title"/>
  <IBSelectableSimple out_data_path="event" select_value="reset_racenet" help_text="lng_select"/>
  <IBDataEnabled data_path="reset_racenet.enabled" watch_data="true"/>
  <BVisibilityControlData data_path="reset_racenet.visible" watch_data="true"/>
  <IBItemFlowIndex index="3" index_data_path="selected_index"/>
  <IBAnimated/>
</Item>
```

Combo (`basic_graphics`), CONFIRMADO: `<BSwitchStatic object="toggle_15col"/>` + `IBComboTextStatic`/`IBComboTextData` com `text_glyph="text_value"` (veja "Combos").

Painel de descrição por dados (`profile_save_management`), CONFIRMADO:

```xml
<BTextData glyph="smart_contextual_info.text_title" data_path="sidebar.title" format_id="localise" watch_data="true"/>
<BVisibilityControlStatic glyph="smart_contextual_info.text_title" visible="true"/>
<BTextData glyph="smart_contextual_info.text" data_path="sidebar.description" format_id="localise" watch_data="true"/>
<BVisibilityControlStatic glyph="smart_contextual_info.text" visible="true"/>
<BVisibilityControlData data_path="sidebar.visible" glyph="smart_contextual_info" watch_data="true"/>
```

Rodapé `apply`: `profile_save_management` não tem rodapé e funciona. Pode remover: CONFIRMADO (tela existente).

`SBScrollableItemFlow`: o texto é a lista de ids separada por `\r\n` + 8 espaços, terminando em `\r\n` + 6 espaços. Exemplo real: `'\r\n        item_0\r\n        item_1\r\n        item_2\r\n        item_3\r\n      '`. Os `.xml` convertidos juntam tudo numa linha; o binário tem CRLF.

### E. Host e páginas

| Item | Resultado | Marca |
| :--- | :--- | :--- |
| `data_parent_override="dr2hook_hub"` | O carregador `0x140d24220` grava a raiz `ui.dr2hook_hub` em `tela+0x60`. `out_data_path="event"` vira `ui.dr2hook_hub.event`. | CONFIRMADO |
| Página sem estado | `0x1401ba940` lê `tabs.info[i].screen` e chama `0x140d32960(mgr, &h, nome, …, telaMãe)`. Nenhum estado participa. As páginas de `graphics_calibration` não são referidas por estado nenhum. | PROVÁVEL-forte |
| Mesmo World | As páginas de `graphics_calibration` estão no mesmo World do host. Mantenha assim. | PROVÁVEL |
| Rótulo da aba | `0x1401c1170` lê `tabs.info[i].label` com `format_id` nulo (`r9=0` antes de `0x140d04a30`). O CreateTabs original grava texto já traduzido. Grave o texto final. | PROVÁVEL |
| LB/RB | O `SBTabGroup` troca de aba sozinho (ícones `smart_tabs.controller_icon_left/right`). As páginas não devem ter hot button em LB/RB. | PROVÁVEL |
| `back` | O host não tem `SBHotButtonScreenEvent`; cada página tem o seu. Com override, `back` vai para `ui.dr2hook_hub.event` → `0x1402d5ad0` → `+0x88` → link `back` do fluxo. | PROVÁVEL |

### F. Riscos

| Risco | Situação | Marca |
| :--- | :--- | :--- |
| Thread do `+0x80` | Chamado pelo Enter no tick do runner de fluxo, a mesma volta que atualiza a UI. O store tem lock, então outras threads são toleradas. | PROVÁVEL |
| Objeto de página C++ (`estado+0x290`) | Não é usado. O `StateScreenFECore` não tem esse vetor. A página é aberta pela UI; o TabController (`0x1402e42f0`) só servia para observar `ui.<página>.event`, e o override dispensa isso. | PROVÁVEL-forte |
| Tipo do nó | Criar com um setter e gravar com outro de tipo diferente dá erro 5. Use sempre `0x1401ea8a0` para int. | CONFIRMADO |
| Lock recursivo | Deadlock. Não aninhe. | CONFIRMADO |
| Enter sem `+0x80` | Se o holder já existia, `+0x80` não roda. Os nós também já existem. | CONFIRMADO |
| Nome de tela inexistente em `tabs.info[i].screen` | Comportamento de `0x140d32960` desconhecido. | HIPÓTESE |

### Estrutura recomendada

1. `screens.bin`: o host `dr2hook_hub` (clone de `graphics_calibration`, sem `requested_index_check_data_path`) e as páginas `dr2hook_page_main`/`dr2hook_page_mods` (`smart_screen`, `data_parent_override="dr2hook_hub"`), no mesmo World.
2. Os estados continuam `StateScreenFECore` com `screen_name="dr2hook_hub"`.
3. DLL, `+0x80`: cria `tabs.current_index`, `tabs.info[0..1].{screen,label}`, os índices dos combos (`main.<opção>.index`) e os textos do painel.
4. DLL, a cada quadro (hook de visibilidade, thread da UI): trava, lê os índices por hash, destrava e aplica a mudança.
5. DLL, `+0x88`: trata `select_value` dos botões; `back` segue o fluxo.

Status: tudo acima é especificação. Nada disso foi testado no jogo.

### Uso no DR2 Hook

A estrutura recomendada acima está implementada em `src/core/ui_patch.cpp` (dados) e `src/core/native_screen.cpp` (slot `+0x80`, leitura dos combos, textos). Diferenças: os combos são `IBComboTextData` com a lista criada pela DLL, para que choices tenham qualquer número de valores; o painel da direita usa `BTextStatic` com chaves `lng_dr2hook_*`, respondidas pela busca de idioma, em vez de `BTextData`; as linhas não têm `IBItemFlowIndex`. Antes de chamar cada função do store, a DLL confere os bytes iniciais dela. Estado e pendências em [UI Data](ui_data.md#telas-do-dr2-hook).

## Painel da direita por linha e títulos (2026-10-01)

Validado no jogo na tela `dr2hook_mod` (Practice Mode).

**Painel que acompanha o foco.** O jogo faz isso em `profile_save_management` e `input_bindings`:
- cada `Item` da lista tem `<IBItemFlowIndex index="N" index_data_path="selected_index"/>`, que grava o índice da linha em foco em `ui.<tela>.selected_index` ao navegar (teclado e mouse);
- o painel usa `<BTextData glyph="smart_contextual_info.text_title" data_path="sidebar.title" format_id="explicit|localise" watch_data="true"/>` e o mesmo com `smart_contextual_info.text` e `sidebar.description`. Com `watch_data`, o texto muda quando o nó do store muda.
- **O `IBItemFlowIndex` não cria o nó**: só grava se `selected_index` já existir. Nas telas do jogo quem cria é o estado C++. Sem criar `selected_index` (inteiro) no Enter, o painel fica parado no primeiro texto (confirmado varrendo a memória: o hash de `ui.dr2hook_mod.selected_index` não existia no store, só `sidebar.title` e os combos).
- Na DLL: `PopulateMod` cria `selected_index = 0` e o contêiner `sidebar`; `NativeScreenTick` (thread da UI, a cada 15 ms) lê `selected_index` pelo hash e, se mudou ou o menu mudou de versão, escreve `sidebar.title`/`sidebar.description` fora da trava do store (o `SetString` trava sozinho; o spinlock não é recursivo).
- `format_id="explicit"` mostra o texto como veio; `\n` quebra a linha (confirmado no jogo). Um `\n` no começo de linha (linha vazia, `\n\n`) aparece como caractere desconhecido; o `Menu.describe` troca a linha vazia por um espaço e, por precaução, descarta `\r` (correção ainda não vista na tela).

**Títulos.** Numa `smart_screen`, o título pequeno em vermelho acima (com `/ `) vem do `SBScreenTitle string_id`, e o grande vem do `BTextStatic` do `Item id="title"` (glyph `screen_header_text`). As telas do jogo usam a mesma chave nos dois; a DLL usa `lng_dr2hook_crumb_mod` (nome do mod) no `SBScreenTitle` e `lng_dr2hook_title_mod` ("OPTIONS") no cabeçalho.

**ABI.** `Dr2MenuOption` ganhou `description` (texto do painel), e por isso `kCoreAbiVersion` passou a 2: um `dxgi.dll` antigo recusa o core novo e vice-versa.

## Entrada no menu principal (investigação, 2026-10-01; ainda não implementado)

Hoje o DR2 Hook só abre pela pausa: `PatchScreens` reaproveita o item `reset_view` do `pause_menu` (evento `dr2hook`) e `PatchFlow` acrescenta o link `dr2hook` → hub em todo nó do fluxo que tem link `options`.

- Menu principal: tela `main_menu` (`object="main_menu"`, `SBTabGroup`; as abas vêm de `tabs.info`, preenchido pelo estado C++), nó do fluxo `102601274` (`jump_id="main_menu_hub"`, estado `StateScreenMainMenu` 1984269910). Esse nó **não tem** link `options`: trata cada bloco por um link próprio (`game_settings`, `input`, `profile`, `racenet_profile`, `graphics`, `audio`, `legal`, `credits`, …). Por isso o patch atual não chega nele.
- Aba "Options & Extras": tela `options_extras` (`object="options_extras"`), grade de 8 blocos com posição fixa na cena (`tile_preferences`, `tile_input`, `tile_profile`, `tile_racenet`, `tile_graphics`, `tile_audio`, `tile_legal`, `tile_credits`) e `SBGridItemFlow` para a navegação. Cada bloco: título/subtítulo `BTextStatic`, marca d'água `BTextureStatic texture="tile_watermark_*"`, `IBSelectableSimple select_value=<evento>`.
- Um bloco novo exigiria uma posição (`tile_*`) que a cena não tem. O caminho de menor risco, igual à pausa: **reaproveitar um bloco** (ex.: Credits ou Legal), trocando texto e evento para `dr2hook`, e estender `PatchFlow` para também ligar `dr2hook` → hub no nó com link `credits` (o `back` do hub volta para o menu principal).
- Alternativa não explorada: acrescentar uma aba "DR2 Hook" ao `tabs.info` do `main_menu` no Enter do estado (a DLL já cria abas no próprio hub), com páginas que usam o menu principal como raiz de dados.

### Aba "DR2 Hook" no menu principal (implementado e validado no jogo em 2026-10-01)

- Abas do menu principal: `0x1402ff0b0` (classe `StateScreenMainMenu`, não é da família de `0x1402c2b90`). Para cada aba: `rótulo = 0x140559f20("lng_<x>_tab_label")` e uma entrada `{screen, label, bool}` (0x18) numa lista local `{ptr, capacidade=8, contagem}`; em paralelo, um código por aba no estado (`+0x2a0` ptr u32, `+0x2a8` capacidade, `+0x2b0` contagem; ex.: 3 = `store_hub`, 2 = `options_extras`). Abas, nesta ordem e conforme condições: `my_team`, `other_modes` (Jogo Livre), `esports`, `esports_wrx`, `cme_menu` (Colin McRae), `store_hub` (Loja), `options_extras`. Depois chama o Setup do TabController `0x140369320(estado+0x128, &lista, índice, "")`.
- O código só é lido em `0x140312490` (`código[aba atual] == 0` → `0x140454c10`).
- DLL: hook do `Setup` acrescenta `{dr2hook_mm, "DR2 Hook", 1}` quando a lista tem `options_extras`, ainda há espaço e os códigos estão alinhados (código 2). `ui_patch` cria a tela `dr2hook_mm` (cópia de `options_extras` com só o bloco `preferences`, textos `lng_dr2hook_mm_*`, evento `dr2hook`, sem o atalho de trial) e liga `dr2hook` → hub no nó `jump_id="main_menu_hub"` (os nós hub/mod ficam junto do alvo de `game_settings`).

### Texto rico (2026-10-01)

Os textos do `.lng` usam marcação própria: `{v}` no início liga a marcação, `{s:<estilo>}` troca o estilo (`20_black_wt_sub` negrito, `20_regular_wt_body` normal, `24_black_wt_title`, `_N_din_bold`), `{t:<chave>}` inclui outro texto, `{p}` e `[NOME]` são substituições. Testado no painel do DR2 Hook (`BTextData format_id="explicit"`): a marcação aparece **crua**. Hipótese a testar: com `format_id="localise"` e o painel apontando para uma chave `lng_dr2hook_*` (respondida pelo hook da busca de idioma, com sufixo variável para o `watch_data` perceber a troca), o texto passaria pelo mesmo caminho dos textos do jogo e a marcação seria aplicada.
