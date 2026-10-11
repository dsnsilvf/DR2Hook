# Pistas próprias

Como uma pista feita no editor (viewer3d) chega ao jogo. Hoje só o DR2 Hook Ring
(`examples/tracks/synthetic__dr2hook_ring`) tem o porte completo; o caminho abaixo é o dele.

## 1. Para o jogador: instalar um pacote

1. Instale o DR2Hook ([instalação](install.md)).
2. Copie o arquivo `<pista>.dr2track` para `<jogo>/ctracks/` (crie a pasta se não existir).
3. Abra o jogo. A tela de boot do DR2Hook mostra a montagem: "N track package(s) found in ctracks/" e, no fim,
   "N custom track(s) ready (cache built in X s)".

A pista aparece na aba DR2Hook da tela de locations (F3/F4 trocam as abas), com o card, o alfinete no mapa-múndi e
a tela de carregamento próprias.

A dxgi monta um cache em `ctracks/_cache` e só o refaz quando um pacote, o exe ou a versão do carregador muda. Nas
outras aberturas a tela de boot diz "cache up to date". Para tirar a pista, apague o `.dr2track`.

## 2. Para quem faz a pista: do editor ao jogo

### Informações da pista

No viewer3d, **Arquivo > Informações da pista** (ou o botão no Inspector, com nada selecionado). A janela edita
`<pista>/package.json`:

| Campo | O que é |
|---|---|
| Nome | o nome no card e nas listas |
| País | texto do card, em inglês como os do jogo. Escolher na lista (34 países) também põe o alfinete no país |
| Descrição, Autor, Versão | vão para o manifesto do pacote |
| Mapa-múndi | clique para pôr o alfinete; "Tirar" deixa a pista sem alfinete |
| Foto do card | a câmera da foto (ver abaixo); sem ela, o card usa a foto aérea da tela de carregamento |

O mapa da janela é o do próprio jogo. Na primeira vez, o botão **Extrair do jogo** roda
`scripts/research/world_map_image.py`, que grava `build/re/world_map.webp` (fora do repositório).

**Foto do card:**

1. Enquadre a pista no 3D com a janela aberta. As faixas coloridas mostram o que cada imagem do jogo recorta do
   centro da foto:
   - **card**: 560x272, a mais larga;
   - **card grande**: 608x400;
   - **painel**: 352x240.
2. Clique em **Usar a vista atual**. Isso guarda a câmera e a proporção da vista.
3. **Olhar por ela** volta a câmera do 3D a esse enquadramento.

Depois de editar, clique em **Gravar**.

Exemplo de `package.json`:

```json
{
 "name": "DR2 Hook Ring",
 "country": "Brazil",
 "description": "Pista sintética do DR2 Hook, gerada pelo editor de níveis.",
 "author": "DR2Hook",
 "version": "1",
 "map": {"lat": -15, "lon": -48},
 "card": {"camera": [0.6, 0.5, 680, -15, 100, -5], "aspect": 1.77778}
}
```

`card.camera` é a câmera orbital do viewer3d (yaw, pitch, distância, alvo x, y, z), a mesma do `--camera`.

### Testar no jogo (F5)

O botão verde do editor roda `scripts/research/ring_deploy.py`, que faz, nesta ordem e só com o que mudou:

1. porta a pista para a overlay de desenvolvimento (`captures/overlay`): terreno, objetos, traçado, câmeras do
   replay, vagas de largada e tela de carregamento;
2. **card**: renderiza a foto do card com o viewer3d (2240 px de largura, na proporção marcada), se o
   `package.json` tiver `card`;
3. **package**: gera o `.dr2track` com `scripts/research/track_package.py` e o instala em `<jogo>/ctracks/`. O zip
   sai igual byte a byte para a mesma entrada, e o instalado só é regravado quando muda; assim o cache do jogo só
   é refeito quando a pista mudou;
4. marca a abertura como vinda do editor e abre o jogo pela Steam.

Numa abertura pelo editor, o jogo pula a tela de boot do DR2Hook, o splash e o vídeo da Codemasters, e vai direto
para a especial. Detalhes em [Abertura do jogo](../reverse_engineering/boot_intro.md) §6.

Só portar, sem abrir o jogo:

```bash
python3 scripts/research/ring_deploy.py --no-game --quick
```

### Gerar o pacote à mão

```bash
python3 scripts/research/track_package.py --overlay captures/overlay --id dr2hook_ring --info examples/tracks/synthetic__dr2hook_ring/package.json --card build/re/ring_deploy/card/card.ppm --out build/re/ring_package --install "<jogo>/ctracks"
```

Argumentos explícitos (`--name`, `--country`, `--lat`, `--lon`…) valem por cima do `--info`.

## 3. O que vai no `.dr2track`

O pacote é um zip; o formato está em `src/trackpkg/package.h`.

| Entrada | Conteúdo |
|---|---|
| `manifest.json` | id, nome, país, descrição, autor, versão, hospedeira, rotas e `map` |
| `track/` | só os arquivos da pista que diferem dos da hospedeira; o resto o jogo lê da hospedeira |
| `data/` | a tela de carregamento (`<pista>_<rota>.tpk` e `_spline.tpk`) |
| `graft/` | fragmentos enxertados nos `.pssg` do jogo: componente da tela de carregamento e as texturas do card (`s_$LOC`, `s_$LOC_l`, `panel_$LOC`) |
| `preview/card.png` | o card em 560x272, para listas e editores; o jogo ignora |

Manifesto do Ring:

```json
{
 "format": 1,
 "id": "dr2hook_ring",
 "name": "DR2 Hook Ring",
 "country": "Brazil",
 "host": {"location": "portugal", "track": "montalegre_rallycross"},
 "routes": [{"id": "route_0", "host_route": "route_0", "name": "Circuito Completo", "length_m": 2132.4}],
 "map": {"lat": -15.0, "lon": -48.0}
}
```

## 4. Alfinete do mapa-múndi

O mapa da tela de locations (`world_map`, 448x272) ocupa o retângulo x 0..4,4, y −2,64..0 do nó `image!2`. Os
marcadores dos países seguem um ajuste linear em graus, e em frações da imagem (u para a direita, v para baixo)
fica assim:

```
u = (1,96478 + 0,0129152 · lon) / 4,4
v = (1,74648 − 0,0157565 · lat) / 2,64
```

O jogo não aceita um `persistentDB.pssg` maior que o original. Por isso as pistas próprias reusam quatro marcadores
livres (sobras do DR1) e só movem a posição deles. Com mais de quatro pistas, algumas dividem um marcador. Detalhes
em [Tela de locations](../reverse_engineering/location_select.md).

## 5. Limites de hoje

- Só o Ring tem porte para o jogo. Outras pistas do editor ainda não saem como pacote.
- A pista nova usa uma hospedeira, hoje Montalegre (rallycross). O jogo carrega dela o que o pacote não traz.
- A lista de pacotes do jogo é fixa no exe. O DR2Hook monta o catálogo no cache em vez de mudar essa lista.
