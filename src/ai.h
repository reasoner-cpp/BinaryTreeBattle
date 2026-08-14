#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>
#include <memory>
#include <random>
#include <utility>

using Gdiplus::PointF;
using Gdiplus::Color;

// ===== 地图尺寸 (设置页可调, 节点实际大小不变) =====
inline int WIN_W = 1000, WIN_H = 700;      // 地图区尺寸 (默认 1000x700)
inline int PANEL_W = 300;                  // 右侧信息面板宽度
inline int FULL_W = WIN_W + PANEL_W;       // 总窗口宽度

// ===== 共享数据结构 =====
struct ScorePoint {
    PointF pos; int value = 1; bool alive = true;
};
struct Node {
    PointF pos; Color team; Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
    int edgeStrength = 1;    // 线段强度: 创建时str决定, 穿越攻击-1, 归零→子节点孤立化
    int level = 1;           // 节点等级(独立): 右键加强1-5, 形状显示, 孤立后每回合-2
    int attack = 1;          // 节点攻击力: 分支伤害=攻击力 (1..attackMax, 默认1)
    bool removed = false;    // 节点已死 (从场移除) 但仍持有孤立子节点
    bool isolated = false;   // 孤立节点 (连接被切断, 每回合level-2, 可接回)
    int rid = -1;            // 对局内唯一节点 ID (回放精确定位, 根=0红/1蓝)
};

struct SimState;  // 搜索用模拟局面 (前向声明, 定义在 ai.cpp)

// ===== AI 可调参数 (用于自我对抗搜索) =====
struct AIConfig {
    // 攻击权重 — 新量级: 以"收集1个黄点≈70"为基准单位
    // 关键纪律: 命中普通叶子节点价值很低, 击杀枢纽/大杀/推进才有高回报
    float nodeHit    = 248.15f;  // 命中敌方节点 (叶子命中≈120, 不鼓励)
    float hubFactor  = 120.30f;   // 枢纽价值乘数 (子树越大越值钱: 枢纽=120+90*子树)
    float edgeKill   = 76.70f;   // 摧毁敌方边
    float edgeHit    = 28.56f;   // 削弱敌方边 (每级强度折价)
    float decisive2  = 207.66f;  // 一次打掉≥2目标
    float combo      = 43.81f;   // 命中+穿越复合
    float strBonus   = 26.50f;   // 攻击时每级强度奖励 (仅大战果时给)
    float advance    = 3.93f;    // 朝敌根推进乘数 (核心战略: 推进400px≈880)
    // 发展
    float collect    = 76.50f;   // 收集得分点
    float collectLow = 70.86f;   // 缺分时收集
    float expand     = 0.48f;   // 开拓版图
    float center     = 0.18f;   // 占领中心
    float defense    = 1.95f;    // 防守纵深 (堵威胁走廊)
    // 资源管理
    float reserveBase= 2.48f;   // 保底积分基数
    float spendOpen  = 27.32f;   // 开局花费敏感度 (原127过极端)
    float spendMid   = 9.15f;    // 中后期花费敏感度
    float spendTight = 7.90f;   // 积分紧张敏感度
    float extraThresh= 414.62f;  // 额外行动触发评分阈值 (需高价值行动; 原380过低: 买后退移动)
    float threatMul  = 1.46f;    // 威胁惩罚系数
    // ===== 人类策略参数 (12局人类胜方行为提炼, 训练可调) =====
    float reinfBudget   = 0.42f; // 强化预算占富余分比例 (人类强化防线但留进攻分)
    float reinfThreat   = 22.94f; // 强化威胁门槛 (人类强化真正受威胁的边)
    float sprintDist    = 275.78f; // 冲刺区距离 (距敌根≤此值允许大步扩展, 人类e2-e3杀根)
    float sprintBonus   = 1.84f; // 冲刺推进加成 (进入冲刺区推进价值提升)
    float extraSprint   = 210.82f; // 额外行动冲刺触发距离 (贴脸才买X, 防烧分)
    float deepPush      = 420.00f; // 中距离大步推进距离 (分数≥6时e2前插)
    float hubPreference = 1.20f;  // 枢纽击杀偏好 (切大树vs打叶子权衡, >1更爱杀枢纽)
    float riskTaker     = 1.12f;  // 冒险度 (威胁惩罚乘数, >1更谨慎, <1更激进)
};

// ===== AI =====
class AI {
public:
    struct Move {
        Node* parent = nullptr;
        PointF target;
        int strength = 1;
        int extend = 0;
        float score = 0;
    };

    AI(const AIConfig& cfg = AIConfig()) : m_cfg(cfg) {}

    void setDifficulty(int d) { m_difficulty = d; }   // 0简单 1普通 2困难
    const AIConfig& cfg() const { return m_cfg; }

    // ===== 数据驱动学习 (从对局 btbdt 学习胜方行为) =====
    void learnFromReplayFile(const std::string& path);   // 分析单个对局文件
    void learnFromReplayFiles();                         // 扫描当前目录所有 .btbdt
    int gamesLearned() const { return m_gamesLearned; }
    float learnedAggro() const { return m_learnAggro; }
    float learnedDefense() const { return m_learnDefense; }
    float learnedCollect() const { return m_learnCollect; }
    float learnedDepth() const { return m_learnDepth; }
    float learnedSpend() const { return m_learnSpend; }
    float learnedPush() const { return m_learnPush; }
    float learnedScorePref() const { return m_learnScorePref; }
    float extraThreshold() const { return m_cfg.extraThresh; }
    // ===== 参数/学习数据持久化 (ai.dat) =====
    bool loadFromFile(const char* path);   // 从 ai.dat 读全部 AIConfig + 学习数据
    bool saveToFile(const char* path) const;  // 写入 ai.dat (含学习数据)
    void setLookahead(bool b) { m_lookahead = b; }
    bool lookahead() const { return m_lookahead; }
    // 节点攻击力上限 (由游戏设置传入)
    void setAttackMax(int v){ m_attackMax = std::max(1, std::min(5, v)); }
    int attackMax() const { return m_attackMax; }
    // 游戏规则 (由主游戏设置传入; 默认扩展/强化/攻击升级各 1 分, 扩展上限 3 级)
    // 成本模型与主游戏 executeMove 一致, 避免规则非默认时 AI 预算失配 (做出付不起的行动被拒)
    void setRules(int extendCost, int reinfCost, int attackCost, int extendMax){
        m_extendCost = std::max(0, std::min(5, extendCost));
        m_reinfCost = std::max(0, std::min(5, reinfCost));
        m_attackCost = std::max(0, std::min(5, attackCost));
        m_extendMax = std::max(0, std::min(5, extendMax));
    }
    int extendCost() const { return m_extendCost; }
    int reinfCost() const { return m_reinfCost; }
    int attackCost() const { return m_attackCost; }

    // ===== 局面动态分析 =====
    struct Situation {
        int myNodes = 0, enNodes = 0;
        int myScore = 0, enScore = 0;
        float myAvgEdge = 1.f, enAvgEdge = 1.f;  // 平均边强度
        float playerAggression = 0.f;            // 玩家侵略性 0..1
        float playerDefense = 0.f;               // 玩家防守倾向 0..1
        bool playerTurtle = false;               // 玩家龟缩 (重防守少进攻)
        int enFrontCount = 0;                    // 敌方可扩展前线节点数
        float flankGap = 0.f;                    // 己方根周围最大防护缺口(距离)
        int flankGapAngle = -1;                  // 缺口扇区 (0..7, 每45°)
        bool flankThreat = false;                // 是否存在绕后威胁
        float enFlankGap = 0.f;                  // 敌方根周围最大防护缺口
        int enFlankGapAngle = -1;                // 敌方根缺口扇区
        bool noContact = false;                  // 双方尚未正面交锋
        int myWeakEdges = 0;                     // 我方强度1且暴露于玩家的边数
        int myFrontExposed = 0;                  // 我方暴露于玩家攻击的节点数
        float scorePtAdv = 0.f;                  // 黄点资源优势 (正=我方占优)
        int   enNearRoot = 0;                    // 敌方节点距我根<250 (威胁走廊)
        int   myFrontN = 0, enFrontN = 0;        // 前线节点数 (距敌根<300)
        float myFrontEdge = 0.f, enFrontEdge = 0.f;  // 前线边强度总和
        bool  enScoreAhead = false;              // 敌方积分领先
        float myControl = 0.f, enControl = 0.f;  // 半场控制 (对方半场节点权重)
        float enMinDist = 1e9f;                   // 敌方节点距我根最近距离 (杀根威胁预警)
        bool  enProbing = false;                  // 敌方可扩展节点逼近我根(<260) 即将发起杀根
        float myRootShield = 0.f;                 // 我根240px内己方节点数 (根部盾牌密度)
    };

    /// 分析当前局面, 供动态策略使用
    Situation analyzeSituation(const std::vector<Node*>& nodes, Node* myRoot,
                               Node* enemyRoot, int myScore, int enScore,
                               const std::vector<ScorePoint>& scores) const;
    /// 观察玩家一次行动 (0=扩张 1=强化 2=攻击 3=额外行动)
    void observePlayerAction(int type);
    // ===== 自我对弈进化 (参数突变) =====
    void seedRandom(unsigned s);      // 重置随机数 (保证每局变异不同)
    void mutate(float rate);          // 随机突变全部 AIConfig 参数 (±rate 比例), 并夹紧到合理范围

    // ===== 迭代思考 (支持选点动画) =====
    /// 开始思考 (初始化状态, 分析局面)
    void beginThink(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                    int myScore, int enScore, const std::vector<ScorePoint>& scores,
                    const Color& myTeam);
    /// 执行一轮评估; 返回 true 表示思考完成
    bool thinkStep(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                   int myScore, int enScore, const std::vector<ScorePoint>& scores,
                   const Color& myTeam);
    bool thinking() const { return m_thinking; }
    /// 取最终结果
    const Move& result() const { return m_best; }
    /// 保留已评估结果, 开启新一轮扫描 (思考期持续探索)
    void rethink() { m_iter = 0; m_thinking = true; }

    /// 强化薄弱关键边
    void reinforce(std::vector<Node*>& nodes, int& myScore, int enScore, const Color& myTeam);

    // ===== 诊断 (调试用) =====
    struct CandInfo { float score; Node* parent; PointF tgt; int str, ext; };
    /// 返回当前候选 top-N 明细 (thinkStep 结束后调用)
    std::vector<CandInfo> debugTopCands(int n) const;

private:
    struct Cand { float score; Node* parent; PointF tgt; int str; int ext; };
    // 评估单个节点全部目标 (供 thinkStep 分批调用)
    void evaluateNode(Node* n, const std::vector<Node*>& nodes, Node* myRoot,
                      Node* enemyRoot, int myScore, const std::vector<ScorePoint>& scores,
                      const Color& myTeam);
    void finalizeBest(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                      int myScore, int enScore, const std::vector<ScorePoint>& scores,
                      const Color& myTeam);

    float scoreTarget(Node* parent, PointF tgt, int str, int ext,
                      const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                      const std::vector<ScorePoint>& scores, const Color& myTeam,
                      float spendMult);
    float threatPenalty(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                        const Color& enemyTeam, Node* parent, PointF tgt);
    void genTargets(Node* parent, Node* myRoot, Node* enemyRoot,
                    const std::vector<Node*>& nodes, const std::vector<ScorePoint>& scores,
                    const Color& myTeam, std::vector<PointF>& out);
    static int subtreeSize(const Node* n);
    int computeReserve(int myScore, int totalNodes) const;
    /// 强制杀根检测: 若存在积分预算内可一步命中敌根的移动, 直接取胜 (无视其他权重)
    bool findKillMove(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                      int myScore, const std::vector<ScorePoint>& scores,
                      const Color& myTeam, Move& out);

    // ===== 前向模拟 (围棋式 lookahead) =====
    float quickEval(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                    const Color& myTeam) const;
    float quickScore(Node* parent, PointF tgt, int str, int ext,
                     const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                     const std::vector<ScorePoint>& scores, const Color& myTeam) const;
    Move bestFromNode(Node* n, const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                      int myScore, const std::vector<ScorePoint>& scores,
                      const Color& myTeam);
    float simulateLookahead(Node* parent, PointF tgt, int str, int ext,
                            const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                            int myScore, int enScore, const std::vector<ScorePoint>& scores,
                            const Color& myTeam);

    // ===== 启发式 + Alpha-Beta 剪枝搜索 =====
    float heuristicEval(const SimState& st, const Color& myTeam) const;
    std::vector<Move> topMoves(const SimState& st, const Color& team, int K,
                               const Color& myTeam);
    float alphaBeta(const SimState& st, int depth, float alpha, float beta,
                    const Color& turn, const Color& myTeam, int branch);

    // 配置
    AIConfig m_cfg;
    int m_difficulty = 1;      // AI 难度: 0简单(第二优) 1普通 2困难
    bool m_lookahead = true;   // 是否启用前向模拟 (主游戏开, selfplay 训练关)
    // 局面
    Situation m_sit;

    // 迭代思考状态
    bool m_thinking = false;
    std::vector<Node*> m_expandables;
    int m_iter = 0, m_maxIter = 0;
    std::vector<Cand> m_allCands;
    Move m_best;

    // 玩家策略学习
    std::vector<int> m_playerHistory;   // 最近玩家行动
    int m_roundsSeen = 0;

    // 数据学习 (从对局文件学到的行为特征)
    float m_learnAggro=0.5f;    // 攻击倾向
    float m_learnDefense=0.5f;  // 防守倾向
    float m_learnCollect=0.5f;  // 收集倾向
    float m_learnDirX=0.f, m_learnDirY=0.f;  // 胜方平均进攻方向 (单位向量)
    float m_learnDepth=0.5f;    // 胜方进攻深度 (深入敌方半场程度 0~1)
    float m_learnSpend=0.9f;    // 胜方平均攻击花费 (花费纪律: 数据≈0.2~1.0)
    float m_learnPush=0.5f;     // 胜方推进强度 (每步朝敌根缩短量/40)
    float m_learnScorePref=1.0f; // 胜方收集黄点的价值偏好 (收集均值/全局均值, >1 偏好高分点)
    int m_gamesLearned=0;       // 已学习对局数

    // 记忆
    PointF m_lastTarget{0, 0};
    std::vector<PointF> m_recentTargets;   // 最近落点历史 (打地鼠记忆: 反复被摧毁的位置不再重建)
    std::vector<PointF> m_killedTargets;   // 已确认被敌方摧毁的己方节点位置 (危险区域, 重罚重建)
    std::vector<PointF> m_prevMyNodes;     // 上次思考时的己方节点位置快照 (对比出被摧毁的节点)
    int m_turnsWithoutBranch = 0;
    int m_stallTurns = 0;                  // 连续无推进回合 (僵持冲刺触发)
    float m_lastDistToEn = -1.f;           // 上次落点距敌根距离
    std::mt19937 m_rng{42};
    float noise() { std::uniform_real_distribution<float> d(-2.f, 2.f); return d(m_rng); }

    int m_attackMax = 5;                 // 节点攻击力上限 (由游戏设置传入)
    // 游戏规则成本 (由 setRules 传入, 与主游戏一致)
    int m_extendCost = 1, m_reinfCost = 1, m_attackCost = 1;
    int m_extendMax = 3;                 // 最大扩展级数

    static constexpr float NODE_R = 10, ATK_M = 2, OCCUPY_R = 30;
    static constexpr float MAX_D = 120, EXTRA_D = 40;
    static constexpr int   MAX_STR = 5, DEF_STR = 1;
    // 最大分支距离 = 基础距离 + 扩展级数×每级距离 (扩展上限按规则)
    float maxReach() const { return MAX_D + m_extendMax * EXTRA_D; }
};
