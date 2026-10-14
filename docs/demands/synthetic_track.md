# Pista sintética no jogo

> **CONFIRMADO (2026-10-06 a 2026-10-11):** o **DR2Hook Ring** roda no jogo como `portugal / dr2hook_ring / route_0`, com terreno, colisão, objetos, IA, vagas, câmeras de replay, tela de carga e entrada na **aba DR2Hook** via pacote `.dr2track`. O editor **Testar no jogo (F5)** faz o porte e abre a especial. Fonte detalhada: [track_loading.md](../reverse_engineering/track_loading.md) §9–§13 (português).

> **Análise histórica da escada E0–E7:** [../plans/mapa_no_jogo.md](../plans/mapa_no_jogo.md) (atualizado em 2026-10-14 com o estado real).

## A ideia

Dirigir (ou pelo menos ver) uma pista gerada pelo DR2Hook, começando pelo circuito procedural **DR2Hook Ring**.

## O que existe

| O quê | Grau | Fonte |
| --- | --- | --- |
| Gerador em formato do Track Explorer (DR2M, DR2I, `track.json`) e viewers | `CONFIRMED` offline | [synthtrack.md](../tools/synthtrack.md) |
| `objects.ens`, `trees.bin`, `ornaments.bin`, PNGs potência de 2 | `CONFIRMED` (testes) | `tools/synthtrack/tests/` |
| Ring no jogo (overlay + catálogo + alias Montalegre + colisão própria) | `CONFIRMED` no jogo | `track_loading.md` §10–§12 |
| `.dr2track` instalado em `ctracks/` | `CONFIRMED` no jogo (Ring) | [custom_tracks.md](../guides/custom_tracks.md) |
| `replace_files` em `.nefs` com hash NeFS | `CONFIRMED` no jogo (E3, um ornament) | [mapa_no_jogo.md](../plans/mapa_no_jogo.md), `nefs_write.py` |

## O que o plano antigo dizia “fora de alcance” — e como foi resolvido

| Bloqueio antigo | Situação em 2026-10-14 |
| --- | --- |
| Registro no menu / catálogo | **Resolvido** com enxerto de `base.ctpk` no cache (`ctracks/_cache`) e pacote `.dr2track`, não com linha nova no exe. |
| Colisão `.vcqtc` | **Resolvido para o Ring** com `track_jpk.py ring` e alinhamento à largada Montalegre (`start_transform`). Outras pistas: `UNKNOWN` até repetir o pipeline. |
| `replace_files` e tamanho de bloco | **Contornado:** a pista nova não substitui o `tracksplit` inteiro da hospedeira; a overlay serve arquivos soltos. |
| `progress_track` / `ai_track` binários | **Resolvido para o Ring** (`ring_route.py` + transformação). |

## O que ainda falta (Ring)

| Item | Grau |
| --- | --- |
| Jogador dirige (não só o bot do benchmark) | `UNKNOWN` / não implementado |
| Replay com câmeras do Ring no jogo | **Pendente à mão** (XML e `cameralines.cqtc` portados; falta corrida de replay) |
| Oficina / pódio / lona no parque de serviço | Câmeras, holofotes e estepes **portados** (2026-10-11); lona e plataforma do pódio ainda **da Montalegre** — efeito no Ring **não conferido visualmente** |
| Chuva com gotas na tela | `CONFIRMED` que luz/céu mudam; gotas **não** aparecem |

## Rota “vestir a Montalegre” (plano B histórico)

Ainda válida para **pesquisa** (E4–E6 em `mapa_no_jogo.md`): trocar objetos/texturas/vértices dentro do mesmo `.nefs`. O produto escolhido foi **pista com nome próprio** na overlay.

## Primeiro experimento seguro

Offline: `python -m tools.synthtrack -o examples` e abrir `examples/tracks/synthetic__dr2hook_ring` no viewer3d. No jogo: instalar o `.dr2track` ou usar F5 (só Linux, só Ring).

## Fora de escopo

Redistribuir assets do jogo. Texturas e geometria geradas podem ser compartilhadas; pacotes `.nefs` derivados de pistas oficiais, não.
