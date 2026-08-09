/* =====================================================================
 *  Binary Tree Battle — AI 插件接口 (公开 C ABI)
 *  ---------------------------------------------------------------------
 *  允许任何玩家用 C / C++（或其他能导出 C 函数的语言）编写自己的 AI，
 *  编译成 Windows DLL，丢进游戏可执行文件旁的 `ai_plugins\` 文件夹，
 *  在主菜单 vs AI 的难度选择里即可选它作为红方 AI 对手。
 *
 *  插件 DLL 必须导出（extern "C" __declspec(dllexport)）：
 *      const char* aiPluginName(void);
 *      int         aiPluginApiVersion(void);      // 必须返回 AI_PLUGIN_API_VERSION
 *      int         aiPluginGetMove(const AIPluginState*, AIPluginMove*);
 *
 *  可选导出：
 *      const char* aiPluginAuthor(void);
 *      void        aiPluginInit(void);            // 加载时调用一次
 *      void        aiPluginShutdown(void);        // 卸载时调用一次
 *      void        aiPluginThinkStart(const AIPluginState*); // 每回合决策前调用
 *
 *  编译示例（MSVC）：
 *      cl /LD /O2 /std:c++17 /DAI_PLUGIN_BUILD my_ai.cpp ai_plugin.h
 *  编译示例（MinGW g++）：
 *      g++ -O2 -shared -static -static-libgcc -static-libstdc++ -DAI_PLUGIN_BUILD my_ai.cpp -o my_ai.dll
 *
 *  编写时只需包含本文件，链接无需任何附加库。
 * ===================================================================== */

#ifndef AI_PLUGIN_H
#define AI_PLUGIN_H

#define AI_PLUGIN_API_VERSION 1

#ifdef _WIN32
#  if defined(AI_PLUGIN_BUILD)
#    define AI_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#  else
#    define AI_PLUGIN_EXPORT extern "C" __declspec(dllimport)
#  endif
#else
#  define AI_PLUGIN_EXPORT extern "C"
#endif

/* ---- 动作类型 ---- */
#define AI_ACT_BRANCH        0   /* 创建分支：从 parentId 节点伸向 (targetX,targetY) */
#define AI_ACT_REINF_EDGE    1   /* 强化边：targetId = 边的子节点，strength = 目标强度(1~5) */
#define AI_ACT_UPGRADE_NODE  2   /* 加强节点：已随节点加强机制移除而失效 */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 单个节点 ---- */
typedef struct AIPluginNode {
    int   id;              /* 节点编号 (0..nodeCount-1)，仅当次调用有效 */
    int   team;            /* 0 = 红，1 = 蓝 */
    float x, y;            /* 坐标 */
    int   parentId;        /* 父节点编号，-1 = 根节点 */
    int   edgeStrength;    /* 父→本节点这条边的强度 (1..5) */
    int   level;           /* 节点等级 (1..5) */
    int   isolated;        /* 1 = 已孤立（断连，每回合等级-2，不可扩展） */
    int   childCount;      /* 子节点数量 (0..2) */
    int   children[2];     /* 子节点编号 */
} AIPluginNode;

/* ---- 得分点（金色圆点） ---- */
typedef struct AIPluginScorePoint {
    float x, y;
    int   value;           /* 分值 1~3 */
    int   alive;           /* 1 = 在场 */
} AIPluginScorePoint;

/* ---- 候选落点（热力图调试用，可选） ---- */
typedef struct AIPluginCand {
    float x, y;            /* 落点坐标 */
    float score;           /* 该点价值/概率（任意尺度，游戏自动归一化） */
} AIPluginCand;

#define AI_PLUGIN_MAX_CANDS 256   /* 一次最多上报的候选点数 */

/* ---- 传给插件的完整局面 ---- */
typedef struct AIPluginState {
    int   apiVersion;            /* 恒为 AI_PLUGIN_API_VERSION */
    int   nodeCount;             /* nodes 数组长度 */
    const AIPluginNode*    nodes;
    int   scorePointCount;       /* 在场得分点数量 */
    const AIPluginScorePoint* scorePoints;
    int   redScore;
    int   blueScore;
    int   myTeam;                /* 本 AI 所在方：0=红，1=蓝 */
    float maxBranchLength;       /* 最大分支长度（含 3 级扩展） */
    float nodeRadius;            /* 节点半径（碰撞判定） */
    float occupyRadius;          /* 新落点必须与已有节点保持的距离 */
    int   mapWidth;              /* 地图宽 */
    int   mapHeight;             /* 地图高 */
} AIPluginState;

/* ---- 插件返回的走法 ---- */
typedef struct AIPluginMove {
    int   valid;           /* 1 = 走法有效；0 = 跳过/放弃 */
    int   action;          /* AI_ACT_* */
    int   parentId;        /* BRANCH: 从哪个节点伸出 */
    float targetX, targetY;/* BRANCH: 目标坐标 */
    int   strength;        /* BRANCH: 分支强度(1~5)；REINF_EDGE: 目标强度(1~5) */
    int   extend;          /* BRANCH: 距离扩展级数 0~3 */
    int   targetId;        /* REINF_EDGE: 边子节点编号 */
    int   buyExtra;        /* BRANCH: 置 1 表示同时花 3 分买一次额外行动 */
} AIPluginMove;

/* ===================== 必需导出 ===================== */
AI_PLUGIN_EXPORT const char* aiPluginName(void);
AI_PLUGIN_EXPORT int         aiPluginApiVersion(void);
AI_PLUGIN_EXPORT int         aiPluginGetMove(const AIPluginState* state, AIPluginMove* out);

/* ===================== 可选导出 ===================== */
AI_PLUGIN_EXPORT const char* aiPluginAuthor(void);
AI_PLUGIN_EXPORT void        aiPluginInit(void);
AI_PLUGIN_EXPORT void        aiPluginShutdown(void);
AI_PLUGIN_EXPORT void        aiPluginThinkStart(const AIPluginState* state);
/* 可选：每回合决策前把"候选落点价值/概率"填入 out，返回数量。
   返回 0 或未实现时不显示热力图。score 越大越好（蓝→黄→红）。 */
AI_PLUGIN_EXPORT int         aiPluginGetCands(const AIPluginState* state,
                                              AIPluginCand* out, int maxCands);

#ifdef __cplusplus
}
#endif

#endif /* AI_PLUGIN_H */
