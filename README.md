# Binary Tree Battle

A turn-based tree-building tactics game for Windows. Grow a binary tree from your root node, cut through the enemy's nodes and edges, and destroy their root to win.

## Game modes

- **PvP** — local 2 / 4 players, or online (2 / 4 players) — the host's game is the server, others join its IP:port directly
- **vs AI** — Easy / Normal / Hard
- **AI Battle** — built-in AI vs AI, or with plugins
- **Replay** — play back saved games (`.btb`)
- **Settings** — map size, game rules, hotkeys, virtual network

## Highlights

- Self-learning AI — Alpha-Beta search, forward simulation, learns from replays
- Node attack enhancement — right-click a node to boost branch damage (1–5, host-configurable rules)
- Score points are color-coded (1=yellow, 2=orange, 3=red); branches collect any ball their segment passes through
- AI plugin SDK — write your own AI, drop a DLL into `ai_plugins\`
- Online PvP for 2 or 4 players (IPv4) — the host's game directly listens on a port (host-authoritative); friends join the host's IP:port directly, no separate server or room code
- EasyTier / ZeroTier / Tailscale virtual-network support (auto IPv4 detection + one-click launch)
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
| `btbserver.exe` | Standalone relay server (optional; the host now hosts directly) |
| `selfplay2.exe` | Self-play evaluation |
| `sample_ai.dll` | Example AI plugin (`build/ai_plugins/`) |

## AI plugins

Compile your DLL and copy it into `ai_plugins\`. Guide: `docs/AI_Plugin_Guide.md`. Example + build script: `SDK/`.

## Docs

- Game manual (Chinese): `docs/二叉树游戏说明v6.4.0.txt`
- AI plugin guide: `docs/AI_Plugin_Guide.md`

## License

MIT
