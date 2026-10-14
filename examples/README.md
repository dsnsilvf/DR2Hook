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

**Ela roda no jogo.** O `scripts/research/ring_deploy.py`, ou o **Testar no jogo** (F5) do editor, gera o terreno, a colisão, os objetos, as câmeras do replay, as vagas e a tela de carregamento. Depois copia tudo para a overlay de pastas e abre o jogo direto no Ring pelo AutoStage, sem mexer em nenhum `.nefs`. Ainda não tem entrada própria nos menus do jogo, e o carro do teste é o do benchmark, que anda sozinho. Detalhes: [`track_loading.md`](../docs/reverse_engineering/track_loading.md) §9–§12. O `game_transform.json` desta pasta é gravado pelo porte: é o giro e o deslocamento do Ring no jogo, que o editor usa no jogo ao vivo.

### Gerar de novo

Depois de mudar `tools/synthtrack`, grave a pista outra vez aqui:

```bash
python -m tools.synthtrack -o examples
```

O teste `test_example_matches_generator` (`python -m unittest tools.synthtrack.tests.test_synthtrack`) falha se esta cópia ficar diferente do que o gerador produz. O comando também grava `examples/data/tracks.js`, que o `.gitignore` ignora.
