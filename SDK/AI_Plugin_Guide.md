# Binary Tree Battle — AI 插件开发指南

本游戏开放了 AI 插件接口（`ai_plugin.h`，纯 C ABI）。任何玩家都可以用
**C / C++（或其他能导出 C 函数的语言）** 编写自己的红方 AI，编译成
Windows DLL，放进游戏可执行文件旁的 `ai_plugins\` 文件夹，即可在
主菜单 **vs AI → 难度/插件选择** 里选中它作为对手。

---

## 一、快速上手（5 分钟写出第一个插件）

### 1. 包含头文件
只需 `#include "ai_plugin.h"`（游戏根目录已提供），链接时**无需任何附加库**。

### 2. 必须导出三个函数

```cpp
#include "ai_plugin.h"
#include <cstring>

extern "C" {

AI_PLUGIN_EXPORT const char* aiPluginName(void){ return "My First AI"; }
AI_PLUGIN_EXPORT int  aiPluginApiVersion(void){ return AI_PLUGIN_API_VERSION; }

AI_PLUGIN_EXPORT int aiPluginGetMove(const AIPluginState* st, AIPluginMove* out){
    if(!st || !out) return 0;
    std::memset(out, 0, sizeof *out);
    // ... 根据 st 计算走法，填入 out ...
    return 1;
}

}
```

> `AI_PLUGIN_EXPORT` 宏会自动展开为 `extern "C" __declspec(dllexport)`。
> 编译自己的 DLL 时请加 `-DAI_PLUGIN_BUILD`。

### 3. 可选导出（没有也可以）

```cpp
AI_PLUGIN_EXPORT const char* aiPluginAuthor(void){ return "你的名字"; }
AI_PLUGIN_EXPORT void aiPluginInit(void){}                 // 加载时调用一次
AI_PLUGIN_EXPORT void aiPluginShutdown(void){}             // 卸载时调用一次
AI_PLUGIN_EXPORT void aiPluginThinkStart(const AIPluginState*){ /* 每回合决策前 */ }
// 调试热力图：上报"候选落点价值/概率"，游戏以 蓝→黄→红 叠加显示（见第四节）
AI_PLUGIN_EXPORT int aiPluginGetCands(const AIPluginState* st, AIPluginCand* out, int maxCands){
    // ... 把每回合考虑过的候选点填进 out（最多 maxCands 个），返回数量
    return 0;   // 返回 0 就不显示热力图
}
```

### 4. 编译成 DLL

```
MSVC : cl /LD /O2 /std:c++17 /DAI_PLUGIN_BUILD my_ai.cpp
MinGW: g++ -O2 -shared -static -static-libgcc -static-libstdc++ -DAI_PLUGIN_BUILD my_ai.cpp -o my_ai.dll
```

### 5. 放入游戏
把 `my_ai.dll` 放到游戏 exe 同级的 `ai_plugins\` 文件夹里，重新打开游戏。
- **vs AI**：难度选择菜单最下方会出现 `◆ My First AI`，选中后作为红方 AI 对手。
- **AI Battle**：红方/蓝方可分别选内置难度或插件，支持「插件 vs 内置 AI」和
  「插件 vs 插件」，选完两方即可开局。

两个模式里你的 DLL 都会在 AI 列表里以 `◆ My First AI` 出现。

---

## 二、局面数据结构 `AIPluginState`

游戏每次决策前会把当前局面序列化成一个 `AIPluginState`：

```c
typedef struct AIPluginState {
    int   apiVersion;            /* 恒为 AI_PLUGIN_API_VERSION */
    int   nodeCount;             /* 节点总数 */
    const AIPluginNode*    nodes;          /* 所有节点数组 */
    int   scorePointCount;       /* 在场得分点数量 */
    const AIPluginScorePoint* scorePoints; /* 得分点数组 */
    int   redScore;
    int   blueScore;
    int   myTeam;                /* 本 AI 所在方：0=红，1=蓝 */
    float maxBranchLength;       /* 最大分支长度（含 3 级扩展），约 240 */
    float nodeRadius;            /* 节点半径 10 */
    float occupyRadius;          /* 新落点与已有节点的最小距离 30 */
    int   mapWidth;              /* 地图宽 1000 */
    int   mapHeight;             /* 地图高 700 */
} AIPluginState;
```

### 节点 `AIPluginNode`

```c
typedef struct AIPluginNode {
    int   id;              /* 节点编号 0..nodeCount-1，仅当次调用有效 */
    int   team;            /* 0=红，1=蓝 */
    float x, y;            /* 坐标 */
    int   parentId;        /* 父节点编号，-1=根节点 */
    int   edgeStrength;    /* 父→本节点这条边的强度 (1..5) */
    int   level;           /* 节点等级 (1..5) */
    int   isolated;        /* 1=已孤立（断连，不可扩展，每回合等级-2） */
    int   childCount;      /* 子节点数量 (0..2) */
    int   children[2];     /* 子节点编号 */
} AIPluginNode;
```

- 每方各有 **1 个根节点**（`parentId == -1`）。找到自己的根和敌方的根
  通常是你 AI 的第一个步骤。
- 只有 `team == myTeam`、`isolated == 0`、`childCount < 2` 的节点
  才可以作为分支起点。
- 边的强度存在【子节点】上：`nodes[i].edgeStrength` 就是
  `nodes[i].parentId → nodes[i]` 这条边的强度。

### 得分点 `AIPluginScorePoint`

```c
typedef struct AIPluginScorePoint {
    float x, y;
    int   value;           /* 分值 1~3 */
    int   alive;           /* 1=在场 */
} AIPluginScorePoint;
```
落点靠近得分点（约 60px 内）会自动收集。

---

## 三、走法 `AIPluginMove`

```c
typedef struct AIPluginMove {
    int   valid;           /* 1=有效；0=跳过/放弃 */
    int   action;          /* AI_ACT_* */
    int   parentId;        /* BRANCH: 分支起点节点编号 */
    float targetX, targetY;/* BRANCH: 目标坐标 */
    int   strength;        /* BRANCH: 分支强度(1~5)；REINF_EDGE: 目标强度(1~5) */
    int   extend;          /* BRANCH: 距离扩展级数 0~3 */
    int   targetId;        /* REINF_EDGE: 边子节点编号；UPGRADE_NODE: 节点编号 */
    int   buyExtra;        /* BRANCH: 置 1 = 花 3 分买一次额外行动 */
} AIPluginMove;
```

### 三种动作

| 动作 | 宏 | 说明 | 花费 |
|---|---|---|---|
| 创建分支 | `AI_ACT_BRANCH` | 从 `parentId` 节点伸向 `(targetX,targetY)` | `extend + (strength-1)` 分 |
| 强化边 | `AI_ACT_REINF_EDGE` | 把 `targetId` 那条边（父→子）强化到 `strength` | `strength - 当前强度` 分 |

> `AI_ACT_UPGRADE_NODE`（加强节点）已随"节点加强机制"移除而失效，返回该动作会被忽略。
> 节点自身没有护甲：分支穿越节点会削弱其父边强度，边归零则整棵子树被摧毁
> （`AIPluginNode.level` / `isolated` 字段保留但恒为 1 / 0）。

### 分支规则（会被游戏严格校验，返回非法走法会被丢弃）
- 起点：`team==myTeam`、未孤立、`childCount<2`。
- 距离：`20 < dist <= maxBranchLength`，且 `extend` 决定用多少额外距离
  （`reachable = 120 + extend*40`）。
- 落点：不能离任何已有节点小于 `occupyRadius`（30px），也不能出地图。
- 预算：`extend + (strength-1) <= myScore`。
- `strength` 1~5，`extend` 0~3。

### 额外行动 `buyExtra`
当你的分支走法里 `buyExtra=1` 且积分 ≥3 时，游戏会扣 3 分并**再调用一次**
`aiPluginGetMove`，让你走第二步（也必须返回分支）。适合"贴脸杀根"连招。

---

## 四、回合流程（了解即可）

游戏在轮到插件时：
1. 序列化局面 → 调用 `aiPluginGetMove`。
2. 如果返回的是 `REINF_EDGE` / `UPGRADE_NODE`，执行后**再次调用**
   `aiPluginGetMove`（你可连续强化/加强）。
3. 直到返回 `BRANCH` 或 `valid=0`，结束回合。
4. 若分支带 `buyExtra`，扣 3 分后再调用一次拿额外分支。

> 提示：如果你返回一个分支动作，但积分不够 / 落点非法，游戏会直接放弃
> 该步并结束回合。建议自行用 `myScore` 校验预算。

---

## 五、候选落点热力图（调试利器）

插件**可选**导出 `aiPluginGetCands`，每回合决策前游戏会调用它，把它返回的
候选点按 `score` 归一化后，在地图上以 **蓝（低）→ 黄（中）→ 红（高）** 的
热力图叠加显示，并标注百分比。这样你写 AI 时能直观看到：

- 自己的 AI 在考虑哪些落点；
- 各落点的相对优先级是否合理（比如该进攻的点却显示很低，说明评分有 bug）；
- 顺带验证 `occupyRadius` 碰撞、地图边界等约束是否正确避开。

```c
typedef struct AIPluginCand {
    float x, y;      /* 落点坐标 */
    float score;     /* 价值/概率，任意尺度，游戏自动归一化（越大越红） */
} AIPluginCand;

#define AI_PLUGIN_MAX_CANDS 256   /* 一次最多上报 256 个 */

/* 返回填写的候选点数量；返回 0 或未实现 = 不显示热力图 */
AI_PLUGIN_EXPORT int aiPluginGetCands(const AIPluginState* st,
                                      AIPluginCand* out, int maxCands);
```

> 热力图只在插件回合前的等待期（约 0.9 秒）显示，随回合结束消失。
> `score` 只用于该回合内的相对比较，跨回合、跨插件没有可比性。

---

## 六、示例

完整的可编译示例见 SDK 文件夹里的 `sample_plugin.cpp`（策略：先强化薄弱前线边，
再朝敌根推进、顺路收集；并实现了 `aiPluginGetCands` 上报候选热力图）。
可以直接拿它当模板改。Windows 下可用同目录的 `build_plugin.bat` 一键编译。

---

## 七、常见问题

- **游戏菜单里看不到我的插件？**
  确认 DLL 在 exe 同级的 `ai_plugins\` 目录；DLL 是否导出
  `aiPluginName` / `aiPluginApiVersion`（返回 1）/ `aiPluginGetMove`；
  API 版本需与 `AI_PLUGIN_API_VERSION` 一致。
- **插件崩溃导致游戏退出？**
  插件与游戏同进程，越界/死循环会影响游戏本身。请保证
  `aiPluginGetMove` 快速返回（建议 <100ms），不要访问越界节点索引，
  不要返回超出范围的 `parentId/targetId`。
- **可以用 C#/Rust/Python 写吗？**
  只要能导出上述 C 函数并编成 DLL 即可。C# 需用 `[UnmanagedCallersOnly]`
  等导出机制；Rust 用 `#[no_mangle] extern "C"`；Python 可用
  ctypes + 一个薄 C 壳。
