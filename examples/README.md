# Exemplos

## `tracks/synthetic__dr2hook_ring/`: a pista sintética "DR2Hook Ring"

Uma pista **inventada**, sem nada lido do jogo, gerada por [`tools/synthtrack`](../docs/tools/synthtrack.md) com a semente padrão (7). Fica versionada para abrir os viewers e rodar os testes sem o jogo e sem gerar nada antes.

- Circuito fechado de ~2,1 km com relevo, zebras e brita.
- Barreiras, muros de pneus, alambrado, pórtico de largada, arquibancadas, cones, placas e ~420 árvores.
- Duas rotas: `route_0`, com 1011 instâncias, e `route_1`, sem pórtico e arquibancadas e com uma chicane de cones (995 instâncias).
- Terreno com 164 malhas e 151 989 vértices; 16 texturas; ~6,4 MB no total.

Abrir no editor nativo:

```bash
cmake -S tools/viewer3d -B build/viewer3d -G Ninja && cmake --build build/viewer3d
./build/viewer3d/viewer3d --track examples/tracks/synthetic__dr2hook_ring
```

Ela também aparece em **Arquivo → Abrir pista**, como "(exemplo)". Sem `--out`, o Ctrl+S grava as edições em `build/uiview/saves/`; nada é escrito dentro de `examples/`.

O que tem em cada arquivo está em [`docs/tools/synthtrack.md`](../docs/tools/synthtrack.md#o-que-grava). Em resumo:
- `track.json`, `*.bin` e `tex/` estão no formato exportado que os viewers leem;
- `source/` está no formato de origem do jogo (`objects.ens`, `trees.bin`, `ornaments.bin`, PNGs).

**Não é uma pista do jogo:** faltam colisão, `tracksplit.pssg`, dados de progresso e de IA e o registro nos menus. Nunca foi testada no jogo.

### Gerar de novo

Depois de mudar `tools/synthtrack`, grave a pista outra vez aqui:

```bash
python -m tools.synthtrack -o examples
```

O teste `test_example_matches_generator` (`python -m unittest tools.synthtrack.tests.test_synthtrack`) falha se esta cópia ficar diferente do que o gerador produz. O comando também grava `examples/data/tracks.js`, que o `.gitignore` ignora.
