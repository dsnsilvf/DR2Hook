# Exemplos

## `tracks/synthetic__dr2hook_ring/`: a pista sintética "DR2Hook Ring"

Uma pista **inventada**, sem nada lido do jogo, gerada por [`tools/synthtrack`](../docs/tools/synthtrack.md) com a semente padrão (7). Fica versionada para abrir os viewers e rodar os testes sem o jogo e sem gerar nada antes.

- Circuito fechado de ~2,1 km com relevo, zebras e brita.
- Barreiras, muros de pneus, alambrado, pórtico de largada, arquibancadas, cones e placas.
- Decoração: placas de publicidade nas retas, paddock com tendas, caminhões, contêineres e banheiros, torre de controle, fardos de feno e pilhas de pneus nas curvas.
- Natureza: 16 bosques de pinheiros ou bétulas (árvores 3D), árvores soltas, arbustos e pedras.
- Público em barrancos por fora das três curvas mais longas, com alambrado e guarda-sóis.
- Mastros com bandeiras e postes de luz na reta dos boxes, estacionamento com ~110 carros, dois sítios (casas, celeiro, caixa d'água) e um parque eólico de 10 torres nos morros.
- Duas rotas: `route_0`, com 2320 instâncias, e `route_1`, sem pórtico e arquibancadas e com uma chicane de cones (2253 instâncias).
- Terreno com 165 malhas e 152 249 vértices; 26 texturas; ~8,8 MB no total.

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
