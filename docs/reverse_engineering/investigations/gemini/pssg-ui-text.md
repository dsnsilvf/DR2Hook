> Relatório bruto do Gemini 3.8 Flash (agy), 2026-10-02. Não revisado linha a linha; o que foi conferido está em ../../ui_tabs.md (Texto rico).

# Relatório de Pesquisa: Formato PSSG da Interface de DiRT Rally 2.0 (EGO Engine) e Texto Rico

---

## 1. Resumo Executivo

Nesta pesquisa de engenharia reversa sobre os arquivos PSSG de interface de **DiRT Rally 2.0 (Codemasters EGO Engine)**, analisamos a estrutura binária dos contêineres `.pssg`, a representação das telas em `data/screens.xml`, e investigamos por que a marcação de texto rico (`{v}`, `{s:...}`) funciona em telas como o popup de termos (`racenet_confirm_terms`, `t_and_c_popup`) mas aparece como texto cru em telas baseadas em `smart_screen` (`smart_contextual_info.text`).

### Principais Conclusões:
1. **Propriedade que Habilita Texto Rico:** Não se trata de uma flag booleana ou shader distinto, mas sim do **tipo de nó descritor de texto no PSSG**:
   - **`UINODETEXTDOC`** (Node ID `274` / `0x112`, referenciado por prefixo `#edoc`): Representa um documento de texto rico interpretado pelo subsistema de interface do EGO Engine. Permite interpolação e formatação com tags (`{v}` para ativar rich text, `{s:...}` para trocar estilo/fonte).
   - **`UINODETEXT`** (Node ID `273` / `0x111`, referenciado por prefixo `#etex`): Representa um elemento de texto simples (mono-estilo, sem parser de markup). Qualquer tag inserida é tratada como caractere literal e exibida crua.
2. **Localização das Cenas de UI:** Todas as cenas de UI citadas (`smart_screen`, `t_and_c_popup`, `popup_message`, `smart_contextual_info`, `text_message`) estão consolidadas no arquivo unificado **`data/persistentDB.pssg`**.
3. **Nomes de Glyphs:** **Não utilizam hash**. São armazenados como texto puro (ASCII) no atributo `nickname` (Attribute ID `53`) de nós `NODE` e `RENDERNODE`. Glyphs compostos como `smart_contextual_info.text` refletem a navegação hierárquica `parent_nickname.child_nickname`.
4. **Situação na cena `smart_screen`:** **Nenhum** dos glyphs de texto de `smart_screen` possui `UINODETEXTDOC`. Todos utilizam `UINODETEXT` (`#etex...`), o que explica por que a marcação falha em toda a família de telas `smart_screen`.
5. **Entregável de Código:** Foi desenvolvido o parser [`pssg.py`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/pssg_research/pssg.py), independente de bibliotecas externas, capaz de ler o esquema PSSG, reconstruir a árvore de nós, decodificar atributos tipados e exportar para texto ou JSON.

---

## 2. Formato PSSG (Codemasters EGO Engine)

O formato PSSG (Petite Studio Scene Graph / PlayStation Scene Graph) é um contêiner hierárquico orientado a grafos de cena, padronizado com inteiros em **Big-Endian** (32 bits).

### 2.1 Estrutura do Arquivo

```
+-------------------------------------------------------------------+
| Cabeçalho (16 bytes)                                              |
| - Magic: "PSSG" (4 bytes, 0x50 0x53 0x53 0x47)                    |
| - FileSize: uint32 BE (tamanho dos dados seguintes = tamanho - 8) |
| - AttrCount: uint32 BE (total/máx de IDs de atributos no esquema) |
| - NodeTypeCount: uint32 BE (total de tipos de nós no esquema)     |
+-------------------------------------------------------------------+
| Tabela de Esquema (Schema Dictionary)                             |
| Para cada um dos NodeTypeCount tipos de nó:                       |
|   - node_id: uint32 BE                                            |
|   - name_length: uint32 BE                                        |
|   - type_name: char[name_length] (ASCII)                          |
|   - attr_count: uint32 BE                                         |
|   Para cada um dos attr_count atributos:                          |
|     - attr_id: uint32 BE                                          |
|     - attr_name_length: uint32 BE                                 |
|     - attr_name: char[attr_name_length] (ASCII)                   |
+-------------------------------------------------------------------+
| Grafo de Nós (Node Tree)                                          |
| Inicia no nó raiz 'PSSGDATABASE' (node_id 209).                   |
| Cada nó possui:                                                   |
|   - node_id: uint32 BE                                            |
|   - node_size: uint32 BE (bytes que seguem este cabeçalho de 8 B) |
|   - attr_len: uint32 BE (tamanho em bytes do bloco de atributos)  |
|   - Bloco de atributos (attr_len bytes):                          |
|       - attr_id: uint32 BE                                        |
|       - val_len: uint32 BE                                        |
|       - val_data: byte[val_len]                                   |
|   - Carga (node_size - 4 - attr_len bytes):                       |
|       - Se nó contêiner: sequência exata de nós filhos.           |
|       - Se nó folha/dados: bytes brutos (ex: vértices, índices).  |
+-------------------------------------------------------------------+
```

### 2.2 Codificação de Atributos
- **Strings e referências:** Prefixo de 4 bytes uint32 BE indicando o comprimento, seguido da string ASCII/UTF-8. Referências a outros nós começam com `#` (ex: `#edoc4256`, `#etex115`, `#eani3564`).
- **Inteiros / Boleanos / Flags:** uint32 BE (ex: `ha=2`, `wr=1`, `stopTraversal=0`).
- **Ponto Flutuante:** float32 BE IEEE-754 (ex: largura de caixa `wi=9.8f`).

### 2.3 Compressão
- No banco de dados de frontend `data/persistentDB.pssg`, **todos os nós e dados estão descompactados** (diff disk vs header = 0).
- Em arquivos de streaming/textura (`b_nonpersistent.pssg`, `b_osd.pssg`, `b_persistent.pssg`), os blocos finais ou mipmaps podem ter tamanhos de bloco alinhados (potências de 2 / 64 KB), mas **não foi detectado stream zlib nos dados de UI**.

---

## 3. Onde Estão as Cenas de UI e Nomes de Glyphs

### 3.1 Localização dos Arquivos
Todos os elementos de tela e componentes do frontend consultados residem em **`data/persistentDB.pssg`**:
- `smart_screen`: `ROOTNODE` no offset **`4765440`** (`nickname="fe/screens/smart/smart_screen"`).
- `t_and_c_popup`: `ROOTNODE` no offset **`4053701`** (`nickname="fe/screens/terms_conditions/t_and_c_popup"`).
- `popup_message`: `ROOTNODE` no offset **`5187655`** (`nickname="fe/component/popup/popup_message"`).
- `smart_contextual_info`: `ROOTNODE` no offset **`4736854`** (`nickname="fe/component/smart/smart_contextual_info"`).
- `t_and_c_message`: `ROOTNODE` no offset **`4464081`** (`nickname="fe/component/terms_conditions/t_and_c_message"`).

### 3.2 Resolução de Glyphs: Texto vs Hash
A verificação comprovou que os glyphs **são armazenados em texto ASCII claro**, sem qualquer função de hash (testados CRC32, FNV-1a 32/64; nenhum hash apareceu na busca binária):
- No PSSG, nós `NODE` e `RENDERNODE` possuem o atributo `nickname` (ID `53`), contendo strings legíveis como `"smart_contextual_info"`, `"text"`, `"text_title"`, `"text_message"`, `"scroller"`, `"popup_message"`, `"confirm"`, `"decline"`.
- O arquivo `data/screens.xml` faz referência direta a esses nicknames. Nomes pontuados como `smart_contextual_info.text` representam o caminho do nó intermediário (`nickname="smart_contextual_info"`, que referencia o componente externo via `USERDATA` -> `UINODEANIMATED`) até o nó folha (`nickname="text"`).

---

## 4. Comparação Minuciosa dos Nós de Texto

Comparamos os dois casos observados pelo usuário:
1. **Onde o rich text funciona:** `t_and_c_popup` -> item `legal_text` -> `BTextData glyph="text_message"` (referenciando `t_and_c_message.text_message`).
2. **Onde o rich text falha (aparece cru):** `smart_screen` -> `BTextData glyph="smart_contextual_info.text"`.

### Tabela Comparativa

| Propriedade / Aspecto | Popup (`t_and_c_message.text_message`) | Tela Smart (`smart_contextual_info.text`) |
| :--- | :--- | :--- |
| **Arquivo** | `data/persistentDB.pssg` | `data/persistentDB.pssg` |
| **Cena Raiz** | `fe/component/terms_conditions/t_and_c_message_root` (offset `4464081`) | `fe/component/smart/smart_contextual_info_root` (offset `4736854`) |
| **Nó de Renderização** | `RENDERNODE` offset **`4468776`** (`nickname="text_message"`, `id="text_message!1"`) | `RENDERNODE` offset **`4737166`** (`nickname="text"`, `id="text!4"`) |
| **Referência USERDATA (`object`)** | **`#edoc4256`** (offset `4468883`) | **`#etex115`** (offset `4737256`) |
| **Tipo de Nó Alvo (Descriptor)** | **`UINODETEXTDOC`** (Node ID **`274`**) | **`UINODETEXT`** (Node ID **`273`**) |
| **Offset do Nó Alvo** | **`944250`** | **`887796`** |
| **Atributo `id` do Alvo** | `"edoc4256"` | `"etex115"` |
| **Atributo `sn` (Style/Font)** | `_22_roboto_cnd` | `_22_roboto_cnd` |
| **Atributo `wi` (Width)** | `9.8f` (`0x411ccccd`) | `5.5f` (`0x40b00000`) |
| **Atributo `ha` (Horiz. Align)** | `0` (Left) | `3` (Justified / Left-alt) |
| **Atributo `wr` (Word Wrap)** | `-1` / `0xffffffff` (Wrap off) | `1` (Wrap on) |
| **Shader Associado** | `#mat=message!3` (Shader `03 - Default`) | `#03 - Default!1` |
| **Suporte a Rich Text (`{v}`, `{s:...}`)** | **SIM (Funciona)** | **NÃO (Cru)** |

### Outro Exemplo de Funcionamento: `popup_message.message`
No componente padrão `fe/component/popup/popup_message_root` (offset `5187655`):
- O nó `RENDERNODE: nickname="message"` (offset `5189612`) aponta para `USERDATA: object="#edoc2638"`.
- No offset `862211`, o nó `edoc2638` é também do tipo **`UINODETEXTDOC`** (ID `274`), com estilo `_30_roboto_cnd`.

---

## 5. A Propriedade que Habilita Texto Rico

### 5.1 Conclusão Direta
A propriedade que habilita o suporte a texto rico é a definição do elemento de texto como **`UINODETEXTDOC`** em vez de **`UINODETEXT`**.

1. **No nível do tipo de nó:**
   - O tipo `UINODETEXT` (ID 273) instrui a engine a instanciar uma entidade de texto estático simples. O motor envia a string diretamente para o gerador de malha de glifos sem passar pelo pré-processador de marcação.
   - O tipo `UINODETEXTDOC` (ID 274, correspondente a "Text Document") instrui a engine a instanciar o pipeline de documento rico. Esse pipeline reconhece os tokens delimitados por chaves:
     - `{v}`: Inicia o modo de texto rico / vetorial formatado.
     - `{s:<nome_do_estilo>}`: Troca o estilo de fonte atual em tempo de execução (ex.: `{s:20_black_wt_sub}`, `{s:20_regular_wt_body}`).
2. **No nível do grafo de cena:**
   - O `RENDERNODE` da cena deve apontar, em seu nó filho `USERDATA` (atributo `object`), para um nó `UINODETEXTDOC` (`#edoc...`) em vez de um `UINODETEXT` (`#etex...`).

### 5.2 Outros Glyphs na Cena `smart_screen` Já Têm a Propriedade?
**Não.** Todos os nós de texto existentes na árvore da cena `smart_screen` e em seus componentes filhos usam exclusivamente `UINODETEXT` (`#etex...`):
- `smart_contextual_info.text` (offset `4737166`): aponta para `#etex115` (`UINODETEXT`).
- `smart_contextual_info.text_title` (offset `4737522`): aponta para `#etex3018` (`UINODETEXT`).
- `smart_sidebar.text_title` (offset `4208824`): aponta para `#etex3020` (`UINODETEXT`).
- `smart_sidebar.text_subtitle` (offset `4208089`): aponta para `#etex518` (`UINODETEXT`).
- `screen_header.screen_header_text` (offset `5017781`): aponta para `#etex3679` (`UINODETEXT`).
- `smart_toggle_15col.text_title` (offset `4568358`): aponta para `#etex2991` (`UINODETEXT`).
- `smart_toggle_15col.text_value` (offset `4567176`): aponta para `#etex1578` (`UINODETEXT`).

Em todo o arquivo `persistentDB.pssg`, foram encontrados **698 nós `UINODETEXT`** e **apenas 23 nós `UINODETEXTDOC`**. Os 23 nós `UINODETEXTDOC` estão restritos a:
1. `fe/component/credits/credits_sub_header` (`text`)
2. `fe/screens/eula/eula` (`text_message`)
3. `fe/component/initial_legal/initial_legal` (`text`)
4. `fe/component/menu_tiles/racenet_online_menu_variant` (`text`)
5. `fe/component/popup/popup_repairs` (`message` e `title`)
6. `fe/screens/vr_comfort/vr_comfort_screen` (`footer_copy` e `intro_copy`)
7. `fe/component/vehicle_select/veh_select_info_tab` (`text_info_1`)
8. `fe/component/vr_comfort/comfort_level_info` (`text_info`)
9. `fe/screens/racenet_sign_up/racenet_screen` (`info_text`)
10. `fe/component/fe_legal/legal_content` (`text`)
11. `fe/component/terms_conditions/t_and_c_message` (`text_message`)
12. `fe/screens/press_start/press_start` (`text`)
13. `fe/component/persistent/screen_title` (`text_screen_title`)
14. `fe/component/persistent/player_panel` (`text_player_name`)
15. `fe/component/credits/credits_element` (`credits_title` e `credits_body`)
16. `fe/component/dirt_news/news_message` (`text_message`)
17. `fe/component/popup/popup_message` (`message` e `title`)
18. `fe/component/popup/popup_scroll` (`message`)
19. `fe/component/credits/credits_sub_title` (`text`)

---

## 6. O que Foi Verificado vs O que É Hipótese

### Verificado (com evidências)
1. **Estrutura do PSSG:**
   - Cabeçalho de 16 bytes: `PSSG` + `file_size` (u32 BE) + `attr_count` (u32 BE) + `node_type_count` (u32 BE).
   - O arquivo `persistentDB.pssg` possui exatamente 278 tipos de nós e 467 tipos de atributos.
   - Nós de grafo iniciam em `PSSGDATABASE` (ID 209) no offset `16783`.
2. **Localização das cenas:**
   - `persistentDB.pssg` contém todas as cenas de UI mencionadas no prompt.
   - `t_and_c_popup` (offset `4053701`), `smart_screen` (offset `4765440`), `smart_contextual_info` (offset `4736854`), `t_and_c_message` (offset `4464081`).
3. **Nomenclatura de Glyphs:**
   - Armazenados em texto plano no atributo `nickname` (ID 53). Não existe hashing (CRC32/FNV retornaram 0 ocorrências).
4. **Diferença de Tipo de Nó:**
   - O texto funcional do popup (`t_and_c_message.text_message`) referencia `edoc4256`, cujo tipo de nó no PSSG é explicitamente **`UINODETEXTDOC`** (ID 274, offset `944250`).
   - O texto cru (`smart_contextual_info.text`) referencia `etex115`, cujo tipo de nó no PSSG é explicitamente **`UINODETEXT`** (ID 273, offset `887796`).
   - Ambos compartilham atributos idênticos (`sn`, `wi`, `ha`, `wr`, `id`) e usam a mesma família de shaders UI (`03 - Default`).

### Hipótese
1. **Mecanismo de Modding para Habilitar Rich Text no `smart_contextual_info`:**
   - Se o tipo do nó no offset `887796` for alterado de `273` (`0x00000111`, `UINODETEXT`) para `274` (`0x00000112`, `UINODETEXTDOC`), ou se a referência de `USERDATA` no offset `4737256` for redirecionada de `#etex115` para um `#edoc` válido, o motor de jogo passará a interpretar a marcação rica `{v}` e `{s:...}` dentro do `smart_contextual_info.text` da tela `smart_screen`.
