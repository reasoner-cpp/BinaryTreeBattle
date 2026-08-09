# Binary Tree Battle · 二叉树对战模拟器

> **Version V6.2.0** — Win32 + GDI+，纯 C++17，仅依赖 Windows 系统自带库
>
> 双方在地图上构建「二叉树」，利用分支攻击敌方结构，率先摧毁对方根节点的玩家获胜。

![Platform](https://img.shields.io/badge/platform-Windows-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B17-orange)
![GUI](https://img.shields.io/badge/GUI-Win32%20%2B%20GDI%2B-008000)
![License](https://img.shields.io/badge/license-MIT-green)

---

## English TL;DR

**Binary Tree Battle** is a two-player (up to 4 locally, or online) turn-based tactics game written in pure C++17 using Win32 + GDI+. Each player grows a *binary tree* from their root node and attacks by drawing branches that cut through the enemy's nodes and edges. The first player to destroy the enemy's **root node** wins.

Highlights:

- 🎮 **PvP local (2/4 players) · vs AI · AI Battle · Replay · Online** five game modes
- 🤖 **Self-learning AI** — iterative deep think, dynamic situation analysis, Alpha-Beta search, forward simulation, and learning from past replay files
- 🧩 **C-ABI AI plugin SDK** — write your own AI in C/C++/Rust/C#, drop a DLL into `ai_plugins\`, play against it
- 📼 **Replay system** (`.btb`) — full action timeline, seekable progress bar, score-point live stats
- 🌐 **Online PvP** — room-server relay (outbound connect, no inbound port needed)
- 🛠 **Self-play evaluator** (`selfplay2`) + Python analysis tools for `.btb` replays

Build with CMake 3.16+ and MSVC / MinGW. The game links only against libraries shipped with Windows, so the resulting `.exe` is a single portable file.

---

## 游戏简介

《二叉树对战模拟器》（Binary Tree Battle）是一款基于 **二叉树结构** 的对战游戏：

- 每方从地图角落的 **根节点** 开始，用鼠标左键拖拽出「分支」（每节点最多 2 个子节点，即二叉树）。
- 新分支如果 **穿过敌方节点或线段**，会削弱对方的边强度；边强度归零时，**整棵子树被摧毁**；命中根节点则直接获胜。
- 地图上散布 **得分点（金色圆点）**，收集可兑换积分，用于强化边、扩展分支距离、购买额外行动。
- 防守的核心是 **线段强化**（右键边 → 数字键 1~5 → Enter，每级消耗 1 分）：边越强越难切断，而切断一棵大树往往能一举翻盘。

详细的规则与操作手册见 [`docs/二叉树游戏说明v6.2.0.txt`](docs/二叉树游戏说明v6.2.0.txt)。

---

## 特性一览

### 游戏模式（主菜单 ↑/↓ + Enter）

| 模式 | 说明 |
|---|---|
| **PvP 对战** | 本机 2 人对战 / 本机 4 人对战（四角）/ 在线对战 |
| **vs AI** | 你控制蓝方，AI 控制红方（Easy / Normal / Hard，也可选 AI 插件作为对手） |
| **AI Battle** | 红蓝双方自动对战：内置 AI vs 内置 AI、插件 vs 内置、插件 vs 插件 |
| **Replay (.btb)** | 回放已保存的对局录像（可拖动进度条、调速、暂停单步） |
| **Settings** | 地图尺寸、自动保存开关、游戏规则数值、快捷键开关 |

### 核心机制

- **创建分支**：左键点击己方节点拖拽释放；默认 120px，空格可扩展 0~3 级（每级 +40px，消耗积分）。
- **攻击判定**：新分支穿越敌方节点 → 该节点父边强度 -1；穿越敌方边 → 该边强度 -1；强度归零 → 整棵子树摧毁；命中根 → 胜利。
- **线段强化**：右键边 → `1~5` 设目标强度 → Enter 确认。唯一防御体系。
- **积分系统**：初始 10 分；收集得分点（价值 1~3）；强化/扩展/额外行动（X 键，3 分换一次，每回合限 1 次）。
- **Ctrl+Z** 回退一步（联机禁用）、**Ctrl+R** 重开、**B** 边界吸附、**R** 显示节点深度。

### AI 系统（内置）

- **迭代深度思考**：按难度设定思考时长（Easy 1.8~3.2s / Normal 2.5~4.8s / Hard 5.2~8.5s），思考期间实时显示「选点热力图」（蓝→黄→红）。
- **动态局面分析**：玩家侵略性/防守倾向识别、绕后缺口扇区分析、威胁走廊、半场控制、黄点资源优势评估。
- **搜索**：Alpha-Beta 剪枝前向搜索 + 围棋式 lookahead 模拟；困难难度强制「一步杀根」检测。
- **数据驱动学习**：从历史对局文件（`.btb` / `.btbdt`）中学习胜方打法特征（攻击/防守/收集倾向、进攻方向与深度），并写回 `ai_*.dat` 配置文件。
- **AI 配置**：游戏目录下所有 `ai_*.dat` 都会在菜单中列出，可直接编辑参数调节 AI 行为。

### AI 插件系统（公开 C ABI）

任何人都可以用 C / C++（或其他能导出 C 函数的语言）编写自己的红方/蓝方 AI，编译成 DLL 放进 `ai_plugins\` 即可在游戏中选中。

```cpp
#include "ai_plugin.h"

extern "C" {
AI_PLUGIN_EXPORT const char* aiPluginName(void){ return "My First AI"; }
AI_PLUGIN_EXPORT int  aiPluginApiVersion(void){ return AI_PLUGIN_API_VERSION; }
AI_PLUGIN_EXPORT int  aiPluginGetMove(const AIPluginState* st, AIPluginMove* out){
    /* 根据局面 st 计算走法填入 out，返回 1 */
    return 1;
}
}
```

- 必须导出：`aiPluginName` / `aiPluginApiVersion` / `aiPluginGetMove`
- 可选导出：`aiPluginAuthor` / `aiPluginInit` / `aiPluginShutdown` / `aiPluginThinkStart` / `aiPluginGetCands`（候选落点热力图）
- 开发指南见 [`docs/AI_Plugin_Guide.md`](docs/AI_Plugin_Guide.md)，完整示例 + 一键编译脚本在 [`SDK/`](SDK/) 文件夹。

### 联机对战

- PvP → Online：**Host a Room**（输入房间服务器地址，如 `myhost.com:8080`，获得房间码）或 **Join by Room Code**。
- 通过 `btbserver` 房间转发服务器配对（客户端出站连接，无需开放入站端口），数字键 `1~4` 选颜色，主机先手、黄点种子同步。

### 回放系统

- 自动保存对局为 `.btb` 文件（可在设置里分模式开关）。
- 回放完整复现双方动作（分支/强化/额外行动/删除），支持进度条跳转、速度调节（0.5~5.0s/回合）、得分黄点实时统计。

---

## 快速开始（编译）

### 环境要求

- Windows 7+
- CMake ≥ 3.16
- MSVC（Visual Studio 2019+）或 MinGW-w64 g++（≥ 8）

### 构建

```bash
cmake -S . -B build
cmake --build build --config Release
```

CMake 会生成 4 个目标：

| 目标 | 说明 |
|---|---|
| `BinaryTreeBattle.exe` | 主游戏（Win32 + GDI+，静态链接 CRT，单文件免装 VC++ 运行库） |
| `btbserver.exe` | 联机房间转发服务器（运行即开启公网 IPv6 端口，默认 8080） |
| `selfplay2.exe` | 自我对弈强度评估器 |
| `sample_ai.dll` | 示例 AI 插件（输出到 `build/ai_plugins/`） |

> 源码只使用 Windows 系统自带库（GDI+、Winsock、Shell32、Winmm），无需第三方依赖。

### 运行

把 `BinaryTreeBattle.exe` 与默认的 `ai_reasoner_0001.dat`、`settings.dat` 放在同一目录，启动即可。若要使用 AI 插件，将编译好的 DLL 放入 `ai_plugins\` 文件夹。

---

## 目录结构

```
BinaryTreeBattle/
├── CMakeLists.txt            # CMake 构建脚本
├── ai_reasoner_0001.dat      # 默认内置 AI 配置（含 [config] 与 [learned] 学习段）
├── settings.dat              # 默认游戏设置
├── src/                      # 游戏全部源码（v6.2.0）
│   ├── main.cpp              # 主程序：游戏循环 / 渲染 / 输入 / 回放 / 联机
│   ├── ai.cpp / ai.h         # 内置 AI：搜索 / 学习 / 强化 / 动态策略
│   ├── ai_plugin.h           # AI 插件公开接口（纯 C ABI）
│   ├── ai_plugin_host.*      # 游戏侧插件加载器
│   ├── btbserver.cpp         # 联机房间转发服务器
│   ├── selfplay2.cpp         # 自我对弈评估器
│   └── sample_plugin.cpp     # 示例 AI 插件（与 SDK 内一致）
├── docs/                     # 游戏手册 + AI 插件开发指南
├── SDK/                      # 插件开发套件（可整体拷贝）
│   ├── AI_Plugin_Guide.md
│   ├── ai_plugin.h
│   ├── sample_plugin.cpp
│   └── build_plugin.bat      # MinGW g++ 一键编译脚本
├── tools/                    # Python 对局分析工具
│   ├── analyze_btbdt.py      # 批量行为统计
│   ├── analyze_v5.py         # 深度分析：黄点争夺 / 胜负转折
│   ├── deep_analyze.py       # 胜方战术特征
│   └── dump_game.py          # 打印单局完整时间线
└── ai_plugins/               # 放置编译好的 AI 插件 DLL
```

---

## 对局分析工具（Python）

`tools/` 下的脚本用于分析 `.btb` / `.btbdt` 对局文件（需 Python 3，脚本要放在有对局文件的目录下运行）：

```bash
python tools/analyze_btbdt.py     # 批量统计每局的行为模式
python tools/analyze_v5.py        # 黄点争夺 / 胜负转折点
python tools/deep_analyze.py      # 胜方（人类）战术特征
python tools/dump_game.py game.btb # 打印某局的完整时间线
```

### `.btb` 对局格式（BTBDT1）

文本行格式，`[S]` 段记录开局黄点布局，`[A]` 段记录每一步动作：

```
BTBDT1
players=2
red_score=10
blue_score=10
difficulty=1
winner=blue
scores=3
[S]
x=150.0
y=300.0
v=2
[A]
t=0
tm=0
ty=0
px=80.0
py=80.0
tx=240.0
ty2=240.0
s=1
e=0
b=10
a=9
nk=0
ek=0
```

---

## 自我对弈评估（selfplay2）

让两个内置 AI 完全公平自对弈（各 10 分），批量统计胜率、平均回合数与行为指标，并把最后几局存为可回放的 `.btb`：

```bash
selfplay2 [games=50] [seedBase=1000] [threads=1]   # 基本模式
selfplay2 -v [games] [seed]                        # verbose 逐局摘要
```

---

## 常见问题

- **没有 `ai_*.dat`，无法进入 AI 对战？**
  AI 对战要求游戏目录下存在至少一个 `ai_*.dat` 配置文件。仓库默认提供 `ai_reasoner_0001.dat`；删掉后游戏不会自动生成。
- **看不到我的 AI 插件？**
  确认 DLL 在 exe 同级 `ai_plugins\` 目录，且导出了 `aiPluginName` / `aiPluginApiVersion`（返回 `1`）/ `aiPluginGetMove`。
- **在线对战连不上？**
  双方都需能访问同一台 `btbserver`（局域网或公网地址）；若对方已能访问，请确认房间码正确。

---

## 致谢与说明

- 游戏、内置 AI、插件接口、联机服务器与配套工具均由本项目作者使用纯 C++17 完成，未使用任何第三方游戏引擎。
- `settings.dat` 与 `ai_*.dat` 为运行时可写文件，会随游玩/学习自动更新；仓库内提交的是开箱即用的默认值。

## License

[MIT](LICENSE)
