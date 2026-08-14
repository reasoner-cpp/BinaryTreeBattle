# Binary Tree Battle

A turn-based tree-building tactics game for Windows. Grow a binary tree from your root node, cut through the enemy's nodes and edges, and destroy their root to win.

## Game modes

- **PvP** — local 2 players
- **vs AI** — Easy / Normal / Hard
- **AI Battle** — built-in AI vs AI (self-play & evolve)
- **Replay** — play back saved games (`.btb`)
- **Settings** — map size, game rules, hotkeys, AI think time / self-play rounds

## Highlights

- Self-learning AI — Alpha-Beta search, forward simulation, learns from replays
- Node attack enhancement — right-click a node to boost branch damage (1–5, configurable rules)
- Score points are color-coded (1=yellow, 2=orange, 3=magenta); branches collect any ball their segment passes through
- **One-hit kill**: if the AI can strike the enemy root within its score budget, it always kills immediately
- **Self-play evolution** — AI Battle auto-plays up to 1000 games; each round both AIs mutate, the winner learns from the replay and self-upgrades its `ai_*.dat`
- Full mouse support — hover shows "to-click", single-click selects, double-click enters
- Replay system with paged Replays\ folder listing and metadata
- Self-play evaluator (`selfplay2`)

## Build

Prerequisites: Windows, CMake ≥ 3.16, MSVC or MinGW-w64.

```bash
cmake -S . -B build
cmake --build build --config Release
```

| Target | Purpose |
|---|---|
| `BinaryTreeBattle.exe` | Main game |
| `selfplay2.exe` | Self-play evaluation |

## AI configs

AI brains are stored as `AI\ai_*.dat` next to the exe. Pick one when starting vs AI / AI Battle; the winner AI learns & upgrades it during self-play.

## Docs

- Game manual (Chinese): `docs/二叉树游戏说明v6.6.0.txt`

## License

MIT
