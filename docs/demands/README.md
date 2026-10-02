# Demands

Ideas people want from DR2Hook, kept apart from the reverse-engineering notes and from the roadmap that is already planned.

> **Everything in this folder is speculation.** Nothing here is started, promised, or known to be possible. Each document says what makes the idea look possible, what is unknown, and what the first safe experiment would be. If an experiment fails, the idea gets marked as not feasible and stays here as a record.

| Demand | Status | Document |
| --- | --- | --- |
| Multiplayer through live ghost cars | Speculation, first experiment not run | [live-ghosts-multiplayer.md](live-ghosts-multiplayer.md) |
| Map editor | Speculation, not started | [map-editor.md](map-editor.md) |
| Vehicle editor / custom cars | Speculation, not started | [vehicle-editor.md](vehicle-editor.md) |

## How findings are labelled

The same grades as the reverse-engineering docs: `CONFIRMED`, `PROBABLE`, `HYPOTHESIS`, `UNKNOWN`, `REFUTED`. A claim in this folder is only `CONFIRMED` when it links to a note in [docs/reverse_engineering](../reverse_engineering/README.md) that confirms it. Anything else is at most `PROBABLE`.

## Ground rules for every demand

- **Offline and research first.** No demand may touch RaceNet or official leaderboards.
- **Read before write.** Every plan starts with read-only observation (`dr2rec`) before any write to the game.
- **One game build.** Current findings are for one `dirtrally2.exe` build, one car, one session, on Linux with Proton.
