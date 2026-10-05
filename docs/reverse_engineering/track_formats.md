# Formatos de pista (`locations/*.nefs`)

Levantamento de 2026-10-05, feito para o Track Explorer ([../UIVIEW.md](../tools/uiview.md)). Tudo aqui foi **verificado** lendo os arquivos do jogo e conferindo com o resultado exportado (Montalegre rallycross e Poland rally 01), salvo onde está marcado como não decifrado. Nada disto foi testado dentro do jogo.

## Onde ficam

`DiRT Rally 2.0/locations/<país>__<pista>.nefs`: 40 pacotes, cerca de 87 GB. Rallycross tem ~1 GB; rally, 2–4 GB com 4 a 6 rotas. Caminho interno: `tracks/locations/<país>/<pista>/` e, dentro, uma pasta `route_N/` por rota. Em rally cada rota é uma etapa e cobre parte do mapa (Poland 01: 7,8 × 6 km; as rotas 0 e 1 são completas, as 2 a 5 cobrem metades).

## Terreno e pista: `tracksplit.pssg`

PSSG normal (`tools/egodata/pssg.py`, `tools/uiview/mesh.py`). Tem a malha do terreno e da pista; lê em ~0,4 s. O tamanho do arquivo vem das texturas, não da geometria.

- Blocos `batched_track.fx` (a pista densa) não declaram textura e a cor de vértice é branca com alfa 0.
- Blocos `terrain_wsm_*` usam a cor de vértice como **peso de mistura**, não como cor.

## Texturas

- `patchup_ot.pssg` (822 MB) é o pool geral, por nome de `.tga`.
- `objectstextures.pssg` só tem marcadores 4×4; a imagem real vem do `patchup_ot` (preferir a maior).
- Fontes, em ordem: `tracksplit`, `objectstextures`, `treestextures`, `route_objectstextures`, `patchup_ot`, `route_patchup_ot`.
- A textura difusa se escolhe pelo nome: `_d`, depois `_a/_b/_col/_cm/_dif`, depois qualquer que não seja `_n/_s/_e/_m`.

## Objetos físicos: `route_N/objects.ens` e `route_objecttypes.pssg`

XML de texto. Instância: `TEMPLATEENTITYINSTANCE uri="route_objecttypes.pssg#<tipo>"` (atributos `id`, `instanceID`, `instance_tag`, `staticVis`, `ao_map_*`) com `TEMPLATETRANSFORM` de 16 floats em linha-maior (translação nos índices 12 a 14).

Cadeia tipo → malha: `TEMPLATEENTITYREFERENCE` → `TEMPLATEENTITY` (corpo rígido e formas de colisão) → `TEMPLATERENDERABLE uri="objects.pssg#default!N"` → nó `default!N` em `objects.pssg`.

## Árvores: `trees.bin`

Cabeçalho de 72 bytes (contagem em `+48`, início dos registros em `+60`). Registro de 96 bytes: hash do tipo (`+0`), id (`+4`), matriz 3×3 com escala (`+8`, por linhas), posição (`+44`), cor `0xff373737`. O hash é o `reference_id` de `trees_references.xml`; a malha é o nó de `trees.pssg` com apelido `<filename>_x0`. Os primeiros registros são blocos de terreno distante `mnt_dist_*`.

## Ornamentos: `ornaments.bin`

Início dos registros em `+80`, contagem em `+88`, registros de 212 bytes: hash (`+0`), id (`+4`), matriz 3×3 (`+16`), posição (`+52`). Termina com a string `BAKED_0`. O hash vem de `ornaments_references.xml`; a malha é o nó `<filename> Root` ou `_physics` em `objects.pssg`; variantes `nome~a` caem no nome base.

## Traçado: `ai_track.xml`, `progress_track.xml`

XML binário (`tools/egodata/bxml.py`). Portões com posição e normal, limites esquerdo e direito, e divisões (splits).

## Colisão: `track.jpk` (parcial)

Contêiner **JPAK**: cabeçalho `JPAK`, 0, `n`, offset da tabela (16). Entradas de 32 bytes; as últimas 4 palavras são (offset do nome, tamanho, offset dos dados, tamanho). `track.jpk` tem 480 tiles `qt_*.vcqtc` (quadtree de colisão) e `qt.info`. O tile `VCQT` tem caixa mínima e máxima, contagens e nomes de superfície (`SN1+DR2+...`). **Os vértices não foram decodificados.** `drivable_entities.jpk` usa o mesmo contêiner.

## Paisagem de fundo: `landscape.heightfield`

`LAND`/`HMAP`, `uint16` 285 × 284 a partir de `+56`. Paisagem grosseira de fundo; escala e origem não calibradas (o `tracksplit` a substitui).

## Não decifrado

Vértices dos `.vcqtc`, `track.vis`, `grass.grs`, `*.cqtc` (resetlines e cameralines), `crowd_standing2.bin`, `ground_cover.pssg`, `replay_camera_config.xml`, e a textura dos blocos `batched_track.fx`.

## Escrita num `.nefs`

`tools/egodata/nefs_write.py` (`replace_files`): copia o volume uma vez, acrescenta os arquivos novos no fim e atualiza as tabelas; o arquivo novo precisa ter o mesmo número de blocos de 64 KiB. Edição de objetos: [../UIVIEW.md](../tools/uiview.md). **Aceitação pelo jogo: não testada.**
