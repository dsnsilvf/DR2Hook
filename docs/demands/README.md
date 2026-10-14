# Demands

Ideas people want from DR2Hook, kept apart from the reverse-engineering notes and from the roadmap that is already planned.

> **Ideias e status misturados.** Várias demandas ainda são só especulação; outras já têm pedaços no código (Ring no jogo, editor 3D, fantasmas, Debug Mode). Cada arquivo diz o grau de evidência, o que falta e o primeiro experimento seguro.

| Demand | Status | Document |
| --- | --- | --- |
| Multiplayer through live ghost cars | Speculation, first experiment not run | [live-ghosts-multiplayer.md](live-ghosts-multiplayer.md) |
| Map editor | Em andamento: viewer3d + F5 no Ring; `.nefs` editado testado só no E3 (ornament); fluxo oficial é `.dr2track` | [map-editor.md](map-editor.md) |
| Vehicle editor / custom cars | Speculation, not started (the Car Model Explorer reads car models) | [vehicle-editor.md](vehicle-editor.md) |
| Level editor and modeling | Speculation, scope not decided | [level-editor-and-modeling.md](level-editor-and-modeling.md) |
| Quick car preview | Speculation, first experiment not run | [car-preview.md](car-preview.md) |
| Synthetic track in the game | Ring no jogo (overlay + `.dr2track`, aba DR2Hook); jogador ainda não dirige | [synthetic_track.md](synthetic_track.md) |
| TAS, replay and optimal lines | Speculation; base: Practice Mode, GhostLab, `dr2rec` | [tas-and-optimal-lines.md](tas-and-optimal-lines.md) |
| Run comparer and in-game HTML | Speculation; overlay ImGui confirmado; offsets de volta ainda não fechados | [run-comparer.md](run-comparer.md) |

## How findings are labelled

The same grades as the reverse-engineering docs: `CONFIRMED`, `PROBABLE`, `HYPOTHESIS`, `UNKNOWN`, `REFUTED`. A claim in this folder is only `CONFIRMED` when it links to a note in [docs/reverse_engineering](../reverse_engineering/README.md) that confirms it. Anything else is at most `PROBABLE`.

## Ground rules for every demand

- **Offline and research first.** No demand may touch RaceNet or official leaderboards.
- **Read before write.** Every plan starts with read-only observation (`dr2rec`) before any write to the game.
- **One game build.** Current findings are for one `dirtrally2.exe` build, one car, one session, on Linux with Proton.
