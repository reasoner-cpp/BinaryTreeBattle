# Binary Tree Battle

A turn-based tree-building tactics game for Windows. Grow a binary tree from your root node, cut through the enemy's nodes and edges, and destroy their root to win.

## Game modes

- **PvP** — local 2 / 4 players, or online via a room server
- **vs AI** — Easy / Normal / Hard
- **AI Battle** — built-in AI vs AI, or with plugins
- **Replay** — play back saved games (`.btb`)
- **Settings** — map size, game rules, hotkeys

## Highlights

- Self-learning AI — Alpha-Beta search, forward simulation, learns from replays
- AI plugin SDK — write your own AI, drop a DLL into `ai_plugins\`
- Online PvP through a lightweight room relay server
- Replay system with full action timeline and seek bar
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
| `btbserver.exe` | Online room server (default port 8080) |
| `selfplay2.exe` | Self-play evaluation |
| `sample_ai.dll` | Example AI plugin (`build/ai_plugins/`) |

## AI plugins

Compile your DLL and copy it into `ai_plugins\`. Guide: `docs/AI_Plugin_Guide.md`. Example + build script: `SDK/`.

## Docs

- Game manual (Chinese): `docs/二叉树游戏说明v6.2.0.txt`
- AI plugin guide: `docs/AI_Plugin_Guide.md`

## License

MIT
