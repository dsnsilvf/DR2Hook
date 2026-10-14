# Editor de mapas

> **Em andamento (2026-10-14).** O **DR2Hook Editor de Pistas** (`tools/viewer3d`) já edita pistas exportadas e o Ring sintético; o **Testar no jogo (F5)** porta o Ring e abre o jogo. Aceitar **`.nefs` editados de pistas oficiais** no jogo foi validado num experimento pontual (E3, hash NeFS); o fluxo de produto é overlay + `.dr2track`, não substituir pacotes do jogo.

## O que existe hoje

| Peça | Grau | Onde |
| --- | --- | --- |
| Exportar pista do jogo, ver terreno/objetos/IA e gravar `edits.json` | `CONFIRMED` offline | [Track Explorer](../tools/uiview.md), `python -m tools.uiview.track` |
| Aplicar `edits.json` num **novo** `.nefs` (mesmo número de blocos de 64 KiB, hash NeFS recalculado) | `CONFIRMED` por re-leitura e por E3 no jogo (um byte em `ornaments.bin`) | `tools/uiview/track/edit.py`, [track_formats.md](../reverse_engineering/track_formats.md) |
| Editor 3D nativo: mover/girar/apagar, histórico, gizmo, câmeras, vagas, paleta, **F5** | `CONFIRMED` no editor; F5 validado no Ring | [tools/viewer3d/README.md](../../tools/viewer3d/README.md) |
| Pista nova no jogo (Ring) sem mexer em `.nefs` oficiais | `CONFIRMED` no jogo | [track_loading.md](../reverse_engineering/track_loading.md) §10–§13, [custom_tracks.md](../guides/custom_tracks.md) |
| Pacote `.dr2track`, aba DR2Hook, card e alfinete | `CONFIRMED` no jogo (Ring) | [location_select.md](../reverse_engineering/location_select.md) |

## A ideia (escopo completo)

Criar ou mudar especiais: objetos, rotas, terreno, colisão e registro no jogo — para qualquer pista exportada, não só o Ring.

## O que ainda falta

- **Terreno e colisão genéricos:** decodificador/encoder de `.vcqtc` e pipeline estável fora do Ring (`track_jpk.py` cobre o Ring alinhado à Montalegre).
- **Qualquer pista como `.dr2track`:** só o Ring tem porte completo (`track_package.py` + inventário em `track_inventory.py`).
- **Edição direta de `.nefs` no jogo** como produto: possível em teoria (E3); não é o caminho documentado para mods (sem redistribuir assets).
- **Jogador na pista custom:** benchmark/AutoStage ainda; modo livre pendente.

## Primeiro experimento seguro (ainda válido)

Listar arquivos com `egodata` em modo leitura. Para escrever, usar `build/uiview/saves/` e só carregar no jogo com OK explícito do dono.

## Fora de escopo

Redistribuir assets originais do jogo.
