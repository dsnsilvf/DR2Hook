# Tela de locations, card e mapa-múndi

Como a tela "Selecionar local" (`location_select`) monta as abas, o card e o alfinete do mapa-múndi, e o que o DR2Hook muda para as locations criadas por pacotes `.dr2track`. Análise estática do `dirtrally2.exe` (`scripts/research/exe_re.py`), conferida no jogo em 2026-10-09. O código está em `src/core/location_tabs.cpp` (core) e `src/trackpkg/` (dxgi).

Complementa [UI Tabs](ui_tabs.md), [UI Data](ui_data.md) e [Carregamento de pista](track_loading.md).

## Location

Um "local" é um ponteiro para o identificador da linha da tabela LOCATION (`0x7604D91E`):

- `[[local]+8]`: id da location. O jogo vai até 46; o DR2Hook cria a partir de 47 (`kFirstCustomLocation`).
- `[[local]+0x30]`: identificador da disciplina. `[[disc]+8]` é o id da disciplina (1 rally, 2 rallycross, 3 landrush).
- `[[local]+0x48]`: nome interno (`0x140100850`). As locations novas copiam a linha da hospedeira, então herdam o nome dela (ex. `montalegre`).
- `0x1400f3fb0(linha, &tmp)` devolve um ponteiro para o id.

Campos da linha LOCATION que já se sabe: 15 e 16 são latitude e longitude em graus inteiros (varint, complemento de dois). O 17 ainda não tem nome (valores vistos: 182, 91, 39, 13). Os campos 14 e 18 também não. **Os campos 15 e 16 não movem o alfinete do mapa.**

## Abas (F3/F4)

CONFIRMADO. Tela `location_select` com vtable `0x141256e98`. O slot 34 (`0x1402c7830`) faz assim:

1. Preenche o data store e monta a lista de locais em `this+0x170` (contagem em `+0x180`).
2. Chama `0x1402cdc00` para montar as abas (`+0x290`, capacidade `+0x298`, contagem `+0x2a0`).
3. Grava `location_list[].discipline = 0x1400ecc40(disc)` e `quick_nav.list[].icon` (por `0x1400ecc50`: 0 rally, 1 rallycross, o resto "mixed").

A navegação entre abas:

- **F3/F4 (`0x1402d61c0`):** só troca `quick_nav.index`.
- **`0x1402dc000`:** move a seleção. Se ela estava antes da aba nova vai para `aba.first`; se estava depois, para `aba.last`.
- **`0x1402d36b0`:** faz o caminho inverso. Ao mudar a seleção, `quick_nav.index` passa a ser a primeira aba com a disciplina do local, via o call `0x100840` em `0x2d3d2a`.

Armadilha: a fábrica da tela (`0x1402d96f0`, `0x2d0` bytes) guarda as abas dentro do objeto, em `+0x2b0`, com capacidade 2. A terceira aba (DR2Hook) não cabe, então as abas vão para uma página nossa que nunca é liberada (o destrutor `0x1402c3250` só zera a contagem).

O que o DR2Hook muda:

| Ponto | O quê |
| :--- | :--- |
| hook `0x1402cdc00` | locais do DR2Hook no fim; aba própria com disciplina 3 (landrush, que nenhum local do jogo usa) |
| call `0x2c7aaa` (`0xecc40`) | `location_list[].discipline` = 2 (caminho "mixed") para locais ≥ 47 |
| call `0x2d3d2a` (`0x100840`) | disciplina falsa 3, para a seleção não pular para a aba RALLYCROSS |
| lea `0x2c808b` | ícone da aba `icon_dis_dr2hook_small` |
| lea `0x155a3c` → `0x155a59` | selo do card `icon_dis_dr2hook` quando `[rbp+0x67] == 2` |

## Foto do card

CONFIRMADO. O call `0x100850` em `0x2c7bf3` grava `location_list[].location_name`, e o card (`0x140155950`) junta `s_` a esse nome para achar a textura. Para os locais ≥ 47 o DR2Hook devolve `dr2hook_loc<id>` (`CardLocationName`; a string fica na página perto do exe, em `+0x400`). A dxgi enxerta as texturas `s_dr2hook_loc<id>`, `s_dr2hook_loc<id>_l` e `panel_dr2hook_loc<id>` nos bundles `b_locations_(non)persistent_base.pssg`.

## Mapa-múndi

CONFIRMADO, com a correção da chave a validar no jogo.

### Marcadores

Os marcadores ficam em `frontend/databases/persistentDB.pssg` (dentro do `game_1.dat`):

```
ROOTNODE fe/component/map/world_map_root
  USERDATA #fenc!18 (FENODECOUNT c=30: nós abaixo da raiz)
  RENDERNODE image!2
    NODE map_marker_<nome>
      USERDATA #eaniNNNN -> UINODEANIMATED fe/component/map/map_marker (LIBRARY UINODEBASE)
      TRANSFORM  4x4 float big-endian, 64 bytes; posição = floats 12 (x) e 13 (y)
      BOUNDINGBOX
```

Ajuste linear dos marcadores do jogo (posicionados à mão; erro máximo de cerca de 0,17):

```
x = 1.96478 + 0.0129152 · lon
y = -1.74648 + 0.0157565 · lat
```

Há marcadores sem linha LOCATION, sobras do DR1: `argentina`, `la_figuera`, `baumholder` e `argolis`.

**O jogo não aceita um `persistentDB.pssg` maior que o original.** Com um nó a mais (clone do marcador, mesmo com o FENODECOUNT ajustado e a árvore válida) o leitor de stream PSSG crasha em `exe+0x8fa87f`, lendo `0x3f800000` como tipo de nó. Uma edição de mesmo tamanho funciona. (O `PssgPatch` do core só age com o tamanho de cabeçalho 5338789, então não é ele.) Por isso o DR2Hook não acrescenta marcadores: reusa os quatro livres, movendo o TRANSFORM deles (`MoveMapMarker` em `src/trackpkg/builder.cpp`). Com mais de quatro pistas, elas dividem marcadores (`MapMarkerName`: `(id − 47) % 4`).

### Estado do alfinete

- **Behaviour BWorldMap:** fábrica `0x16e980`, vtable `0x122eee0`, update `0x16d880`.
- **Laço dos marcadores (`0x14016fbb0`):** percorre as linhas LOCATION e, para cada uma, pega o nome (call `0x100850` em `0x16fd89`) e chama `0x140170cd0(this, nome)`.
- **`0x140170cd0`:**
  1. `0xd04a30(ctx, this+0x28, nome)` seguido de `0xd00140` lê o estado do data store `world_map/<nome>`. Os estados são: 0 escondido; 1 visível e inativo (`icon_marker_dot_inactive`); 2 ativo (`icon_marker_dot`); 3 ativo com seta (selecionado).
  2. `0xd78a00("%s%s", "map_marker_", nome)` em `0x170d9f` monta o nome do nó. O prefixo está no global `0x1580f80`.
- **Provedor `world_map`:**
  - Criado por `0x140288150`, chamado pela tela em `0x2c788d`.
  - Guarda um mapa id → estado em `+0x28`, preenchido com `0x140242920`.
  - `0x14029c830` cria um nó `world_map/<nome>` por entrada, com o nome vindo do call `0x100850` em `0x29c893`.
  - Atualizações em `0x14029b840`, `0x14029e920` e `0x14029f1d0`, com o nome vindo dos calls em `0x29b89e`, `0x29b9a7`, `0x29ec09`, `0x29ecf4`, `0x29f4bf` e `0x29f595`. Todos usam a tabela LOCATION e o nome só como chave.

Problema: a location nova herda o nome da hospedeira. As duas linhas caem na mesma chave `world_map/montalegre`, e o alfinete que acende é o de Montalegre, em Portugal.

O que o DR2Hook muda (`LocationTabs`, log "patches ... mapa"):

- Os 8 calls de chave (`0x16fd89` e os 7 do provedor) vão para a ponte de `CardLocationName`. Os locais ≥ 47 passam a ter a chave própria `dr2hook_loc<id>`.
- O call `0xd78a00` em `0x170d9f` passa por uma ponte (`g_cave+0x160`) que troca `r9 = dr2hook_loc<id>` pelo marcador livre (`MarkerNodeName`).
- Os dois patches só entram juntos. Só a chave nova, sem trocar o nó, deixaria o alfinete escondido.

## Pacote `.dr2track` e o mapa

O manifesto aceita `"map": {"lat": .., "lon": .., "region": ..}`. O builder da dxgi faz duas coisas:

- grava lat/lon arredondados nos campos 15/16 da linha LOCATION, e `region` no 17, se vier;
- move o marcador livre da location no `persistentDB.pssg` do cache. A edição tem o mesmo tamanho: só a posição muda. Sem `map` no manifesto, o marcador copia a posição do marcador da hospedeira.

Gerar o pacote do Ring, com o alfinete em Brasília por padrão:

```
python3 scripts/research/track_package.py --overlay captures/overlay --id dr2hook_ring --lat -15 --lon -48 --install "<jogo>/ctracks"
```

O cache só é refeito quando a assinatura muda (`kLoaderVersion`, hoje `trackpkg 5`, o exe, os pacotes e o registro).

## Pendente

- Ver no jogo o card novo do Ring (foto pela câmera do `package.json`).
- Nome do campo 17 da LOCATION.

Feito em 2026-10-11: o diálogo no viewer3d (Arquivo > Informações da pista). Ele tem 34 países prontos, o clique no
mapa-múndi do jogo (`scripts/research/world_map_image.py`) e a câmera da foto do card. O passo a passo está em
[Pistas próprias](../guides/custom_tracks.md).
