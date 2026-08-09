#include "ai.h"
#include <cmath>
#include <algorithm>
#include <map>
#include <set>
#include <functional>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Alpha-Beta 剪枝搜索参数
static constexpr int AB_DEPTH = 3, AB_BRANCH = 4;

namespace {
    inline float len2(PointF a, PointF b) {
        float dx = a.X - b.X, dy = a.Y - b.Y;
        return std::sqrt(dx * dx + dy * dy);
    }
    inline bool sameColor(const Color& a, const Color& b) {
        return a.GetValue() == b.GetValue();
    }
    inline bool segCross(PointF p1, PointF p2, PointF p3, PointF p4) {
        auto ccw = [](PointF a, PointF b, PointF c) {
            return (c.Y - a.Y) * (b.X - a.X) > (b.Y - a.Y) * (c.X - a.X);
        };
        auto pe = [](PointF a, PointF b) { return a.X == b.X && a.Y == b.Y; };
        if (pe(p1, p3) || pe(p1, p4) || pe(p2, p3) || pe(p2, p4)) return false;
        return ccw(p1, p3, p4) != ccw(p2, p3, p4) &&
               ccw(p1, p2, p3) != ccw(p1, p2, p4);
    }
    inline float ptSegDist(PointF p, PointF a, PointF b) {
        float dx = b.X - a.X, dy = b.Y - a.Y;
        if (dx == 0 && dy == 0) return len2(p, a);
        float t = std::max(0.f, std::min(1.f,
            ((p.X - a.X) * dx + (p.Y - a.Y) * dy) / (dx * dx + dy * dy)));
        return len2(p, {a.X + t * dx, a.Y + t * dy});
    }
}

// 模拟局面 (深拷贝) — 全局, 与 ai.h 前向声明匹配
struct SimState {
        std::unique_ptr<Node> rootA, rootB;      // 红/蓝根
        Node* myRoot = nullptr; Node* enRoot = nullptr;
        std::vector<Node*> all;
        std::vector<ScorePoint> scores;
        int scoreMy = 0, scoreEn = 0;
        std::map<Node*, Node*> nodeMap;           // 原→克隆
    };
    inline bool isRedC(const Color& c) { return sameColor(c, Color(255, 220, 53, 69)); }

    Node* cloneSub(const Node* src, std::map<Node*, Node*>& m) {
        Node* n = new Node();
        n->pos = src->pos; n->team = src->team; n->edgeStrength = src->edgeStrength;
        m[(Node*)src] = n;
        for (auto& c : src->children) {
            Node* cc = cloneSub(c.get(), m);
            cc->parent = n;
            n->children.emplace_back(cc);
        }
        return n;
    }

    SimState cloneGame(const std::vector<Node*>& all, Node* myRoot, Node* enRoot,
                       const std::vector<ScorePoint>& scores, int myScore, int enScore,
                       const Color& myTeam) {
        SimState st;
        st.rootA.reset(cloneSub(myRoot, st.nodeMap));
        st.rootB.reset(cloneSub(enRoot, st.nodeMap));
        st.myRoot = st.rootA.get(); st.enRoot = st.rootB.get();
        std::function<void(Node*)> collect = [&](Node* n) {
            st.all.push_back(n);
            for (auto& c : n->children) collect(c.get());
        };
        collect(st.myRoot); collect(st.enRoot);
        st.scores = scores;
        if (isRedC(myTeam)) { st.scoreMy = myScore; st.scoreEn = enScore; }
        else { st.scoreMy = enScore; st.scoreEn = myScore; }
        return st;
    }

    // SimState → SimState 深拷贝 (供搜索树分叉)
    SimState cloneSim(const SimState& src) {
        SimState c;
        if (src.rootA) c.rootA.reset(cloneSub(src.rootA.get(), c.nodeMap));
        if (src.rootB) c.rootB.reset(cloneSub(src.rootB.get(), c.nodeMap));
        if (src.myRoot) c.myRoot = c.nodeMap[src.myRoot];
        if (src.enRoot) c.enRoot = c.nodeMap[src.enRoot];
        std::function<void(Node*)> collect = [&](Node* n) {
            c.all.push_back(n);
            for (auto& ch : n->children) collect(ch.get());
        };
        if (c.myRoot) collect(c.myRoot);
        if (c.enRoot && c.enRoot != c.myRoot) collect(c.enRoot);
        c.scores = src.scores;
        c.scoreMy = src.scoreMy; c.scoreEn = src.scoreEn;
        return c;
    }

    void simCollect(SimState& st, PointF p, int& sc) {
        for (auto& sp : st.scores)
            if (sp.alive && len2(p, sp.pos) < 18.f) { sp.alive = false; sc += sp.value; }
    }
    void simKill(SimState& st, Node* node) {
        if (!node) return;
        std::vector<Node*> dead;
        std::function<void(Node*)> sub = [&](Node* r) {
            dead.push_back(r);
            for (auto& c : r->children) sub(c.get());
        };
        sub(node);
        for (Node* n : dead)
            st.all.erase(std::remove(st.all.begin(), st.all.end(), n), st.all.end());
        if (node->parent) {
            auto& sib = node->parent->children;
            sib.erase(std::remove_if(sib.begin(), sib.end(),
                [node](auto& p) { return p.get() == node; }), sib.end());
        } else {
            if (node == st.myRoot) st.myRoot = nullptr;
            if (node == st.enRoot) st.enRoot = nullptr;
        }
    }
    void simAttack(SimState& st, PointF p1, PointF p2, const Color& attacker) {
        std::set<Node*> hits;
        for (auto* n : st.all) {
            if (sameColor(n->team, attacker)) continue;
            if ((n->pos.X == p1.X && n->pos.Y == p1.Y) ||
                (n->pos.X == p2.X && n->pos.Y == p2.Y)) continue;
            if (ptSegDist(n->pos, p1, p2) < 12.f) hits.insert(n);
        }
        for (auto* n : st.all) {
            if (sameColor(n->team, attacker)) continue;
            for (auto& c : n->children)
                if (c && segCross(p1, p2, n->pos, c->pos)) hits.insert(c.get());
        }
        // 先降强度, 再统一 kill; kill 前检查节点仍在场 (防级联悬空)
        std::set<Node*> toKill;
        for (Node* t : hits) {
            if (!t->parent) toKill.insert(t);
            else if (--t->edgeStrength <= 0) toKill.insert(t);
        }
        for (Node* t : toKill) {
            if (std::find(st.all.begin(), st.all.end(), t) == st.all.end()) continue;
            simKill(st, t);
        }
    }
    bool simApplyMove(SimState& st, Node* parent, PointF tgt, int str, int ext, const Color& team) {
        int& sc = isRedC(team) ? st.scoreMy : st.scoreEn;
        int cost = ext + (str - 1);
        if (cost > sc) return false;
        sc -= cost;
        auto nd = std::make_unique<Node>();
        nd->pos = tgt; nd->team = team; nd->parent = parent; nd->edgeStrength = str;
        Node* raw = nd.get();
        parent->children.push_back(std::move(nd));
        st.all.push_back(raw);
        simCollect(st, tgt, sc);
        simAttack(st, parent->pos, tgt, team);
        return true;
    }

// 子树规模
int AI::subtreeSize(const Node* n) {
    int s = 1;
    for (auto& c : n->children) s += subtreeSize(c.get());
    return s;
}

// 保底积分
int AI::computeReserve(int myScore, int totalNodes) const {
    int reserve = 3;
    if (totalNodes < 8) reserve = std::min(5, 3 + myScore / 10);
    else if (myScore <= 5) reserve = 2;
    else reserve = std::min(5, 3 + myScore / 12);
    // ===== 全局观察: 动态保底 (防积分过度消耗) =====
    if (m_sit.enScoreAhead) reserve += 1;              // 敌方积分领先 → 多留
    if (m_sit.enNearRoot >= 2) reserve += 1;           // 敌方逼近我根 → 留防守预算
    if (m_sit.scorePtAdv < 0 && myScore < 8) reserve += 1;  // 黄点劣势 → 攒分
    if (m_sit.flankThreat) reserve += 1;               // 绕后威胁 → 留保底
    // 保底不超过当前分数-1 (确保至少能免费行动)
    if (myScore > 0) return std::min(reserve, myScore);
    return 0;
}

// ===== 强制杀根检测 =====
bool AI::findKillMove(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                      int myScore, const std::vector<ScorePoint>& scores,
                      const Color& myTeam, Move& out) {
    (void)myRoot; (void)scores;
    int reserve = computeReserve(myScore, (int)nodes.size());
    int spendable = std::max(0, myScore - reserve);
    for (auto* n : nodes) {
        if (!sameColor(n->team, myTeam) || n->children.size() >= 2) continue;
        PointF dir{enemyRoot->pos.X - n->pos.X, enemyRoot->pos.Y - n->pos.Y};
        float L = std::sqrt(dir.X * dir.X + dir.Y * dir.Y);
        if (L < 25.f || L > MAX_D + 3 * EXTRA_D) continue;
        dir.X /= L; dir.Y /= L;
        // 根后方落点 (分支穿过根), 多试几个偏移
        for (float extra : {15.f, 30.f, 45.f, 60.f}) {
            float dist = L + extra;
            if (dist > MAX_D + 3 * EXTRA_D) continue;
            PointF tgt{n->pos.X + dir.X * dist, n->pos.Y + dir.Y * dist};
            if (tgt.X < 25.f || tgt.X > WIN_W - 25.f || tgt.Y < 25.f || tgt.Y > WIN_H - 25.f) continue;
            bool occ = false;
            for (auto* o : nodes)
                if (o != n && len2(o->pos, tgt) < OCCUPY_R) { occ = true; break; }
            if (occ) continue;
            int ext = 0;
            if (dist > MAX_D) { ext = (int)std::ceil((dist - MAX_D) / EXTRA_D); if (ext > 3) ext = 3; }
            if (ext > spendable) continue;   // 积分预算内
            if (ptSegDist(enemyRoot->pos, n->pos, tgt) < NODE_R + ATK_M) {
                out.parent = n; out.target = tgt; out.strength = DEF_STR;
                out.extend = ext; out.score = 1e9f;
                return true;
            }
        }
    }
    return false;
}

// ===== 局面动态分析 =====
AI::Situation AI::analyzeSituation(const std::vector<Node*>& nodes, Node* myRoot,
                                   Node* enemyRoot, int myScore, int enScore,
                                   const std::vector<ScorePoint>& scores) const {
    Situation s;
    s.myScore = myScore; s.enScore = enScore;
    float edgeSum = 0; int edgeCnt = 0;
    float enEdgeSum = 0; int enEdgeCnt = 0;
    for (auto* n : nodes) {
        if (sameColor(n->team, myRoot->team)) { s.myNodes++; }
        else { s.enNodes++; }
        for (auto& c : n->children) {
            if (sameColor(n->team, myRoot->team)) { edgeSum += c->edgeStrength; edgeCnt++; }
            else { enEdgeSum += c->edgeStrength; enEdgeCnt++; }
        }
        if (!sameColor(n->team, myRoot->team) && n->children.size() < 2) {
            float d = len2(n->pos, enemyRoot->pos);
            if (d < 300.f) s.enFrontCount++;  // 敌方前线可扩展节点
        }
    }
    s.myAvgEdge = edgeCnt ? edgeSum / edgeCnt : 1.f;
    s.enAvgEdge = enEdgeCnt ? enEdgeSum / enEdgeCnt : 1.f;

    // ===== 根防护: 分析己方根周围 8 扇区保护缺口 (预防绕后) =====
    const float PI = 3.14159265f;
    float sectorProt[8];
    for (int i = 0; i < 8; ++i) sectorProt[i] = 1e9f;
    for (auto* n : nodes) {
        if (!sameColor(n->team, myRoot->team)) continue;
        PointF d{n->pos.X - myRoot->pos.X, n->pos.Y - myRoot->pos.Y};
        float dist = std::sqrt(d.X * d.X + d.Y * d.Y);
        if (dist < 5.f) continue;
        int sec = ((int)std::floor(std::atan2(d.Y, d.X) / PI * 4.f) + 8) % 8;
        if (dist < sectorProt[sec]) sectorProt[sec] = dist;
    }
    float maxGap = 0.f; int gapSec = -1;
    for (int i = 0; i < 8; ++i)
        if (sectorProt[i] > maxGap) { maxGap = sectorProt[i]; gapSec = i; }
    s.flankGap = maxGap;
    s.flankGapAngle = gapSec;
    // 绕后威胁: 缺口扇区内有敌方节点逼近己方根
    if (gapSec >= 0) {
        for (auto* n : nodes) {
            if (sameColor(n->team, myRoot->team)) continue;
            PointF d{n->pos.X - myRoot->pos.X, n->pos.Y - myRoot->pos.Y};
            float dist = std::sqrt(d.X * d.X + d.Y * d.Y);
            if (dist > 280.f) continue;
            int sec = ((int)std::floor(std::atan2(d.Y, d.X) / PI * 4.f) + 8) % 8;
            if (sec == gapSec) { s.flankThreat = true; break; }
        }
    }

    // ===== 敌方根缺口分析 (供主动绕后攻击) =====
    float enSecProt[8];
    for (int i = 0; i < 8; ++i) enSecProt[i] = 1e9f;
    for (auto* n : nodes) {
        if (sameColor(n->team, enemyRoot->team)) continue;
        PointF d{n->pos.X - enemyRoot->pos.X, n->pos.Y - enemyRoot->pos.Y};
        float dist = std::sqrt(d.X * d.X + d.Y * d.Y);
        if (dist < 5.f) continue;
        int sec = ((int)std::floor(std::atan2(d.Y, d.X) / PI * 4.f) + 8) % 8;
        if (dist < enSecProt[sec]) enSecProt[sec] = dist;
    }
    float enMaxGap = 0.f;
    for (int i = 0; i < 8; ++i)
        if (enSecProt[i] > enMaxGap) { enMaxGap = enSecProt[i]; s.enFlankGapAngle = i; }
    s.enFlankGap = enMaxGap;

    // 从玩家历史推侵略性/防守倾向
    if (!m_playerHistory.empty()) {
        int atk = 0, def = 0, total = 0;
        for (int a : m_playerHistory) {
            if (a == 2) atk++;
            else if (a == 1) def++;
            if (a == 0) atk += 0;
            total++;
        }
        s.playerAggression = total ? (float)atk / total : 0.f;
        s.playerDefense = total ? (float)def / total : 0.f;
        s.playerTurtle = (s.playerDefense > 0.6f && s.playerAggression < 0.15f);
    }
    // 双方是否尚未正面交锋 (最近异色节点间距)
    float nearestContact = 1e9f;
    for (auto* n : nodes) {
        if (sameColor(n->team, myRoot->team)) {
            for (auto* m : nodes) {
                if (!sameColor(m->team, myRoot->team)) {
                    float d = len2(n->pos, m->pos);
                    if (d < nearestContact) nearestContact = d;
                }
            }
        }
    }
    s.noContact = (nearestContact > 260.f);

    // ===== 换位思考: 从玩家视角评估我方防守暴露 =====
    // 我方强度1且玩家可扩展节点能打击到的边
    for (auto* n : nodes) {
        if (!sameColor(n->team, myRoot->team)) continue;
        for (auto& c : n->children) {
            if (c->edgeStrength > 1) continue;
            for (auto* en : nodes) {
                if (sameColor(en->team, myRoot->team) || en->children.size() >= 2) continue;
                // 玩家节点能否延伸打击这条边 (粗略: 边距玩家可扩展节点在扩展范围内)
                float d = len2(en->pos, c->pos);
                if (d > 20.f && d < MAX_D + EXTRA_D) { s.myWeakEdges++; break; }
            }
        }
    }
    // 我方暴露于玩家攻击范围的节点 (玩家节点距我方节点 <120 且玩家可扩展)
    for (auto* n : nodes) {
        if (!sameColor(n->team, myRoot->team)) continue;
        for (auto* en : nodes) {
            if (sameColor(en->team, myRoot->team) || en->children.size() >= 2) continue;
            float d = len2(en->pos, n->pos);
            if (d > 20.f && d < MAX_D) { s.myFrontExposed++; break; }
        }
    }

    // ===== 全局观察: 黄点资源评估 =====
    // 每颗黄点按"哪方可扩展节点更接近"折算资源优势
    for (auto& sp : scores) {
        if (!sp.alive) continue;
        float myBest = 1e9f, enBest = 1e9f;
        for (auto* n : nodes) {
            if (n->children.size() >= 2) continue;
            float d = len2(n->pos, sp.pos);
            if (sameColor(n->team, myRoot->team)) { if (d < myBest) myBest = d; }
            else { if (d < enBest) enBest = d; }
        }
        if (myBest < enBest - 40.f) s.scorePtAdv += sp.value;
        else if (enBest < myBest - 40.f) s.scorePtAdv -= sp.value;
        // 势均力敌时不计
    }

    // ===== 全局观察: 威胁走廊 (敌方节点逼近我方根) =====
    for (auto* n : nodes) {
        if (sameColor(n->team, myRoot->team)) continue;
        float d = len2(n->pos, myRoot->pos);
        if (d < 250.f && n->children.size() < 2) s.enNearRoot++;
    }

    // ===== 全局观察: 前线强度对比 (距敌根<300) =====
    for (auto* n : nodes) {
        float dEn = len2(n->pos, enemyRoot->pos);
        if (dEn < 300.f) {
            if (sameColor(n->team, myRoot->team)) {
                s.myFrontN++;
                for (auto& c : n->children) s.myFrontEdge += c->edgeStrength;
            } else {
                s.enFrontN++;
                for (auto& c : n->children) s.enFrontEdge += c->edgeStrength;
            }
        }
    }

    // ===== 全局观察: 半场控制 (节点在对方半场的权重) =====
    // 我方根在左上, 敌方根在右下; 越深入对方半场控制力越强
    for (auto* n : nodes) {
        if (sameColor(n->team, myRoot->team)) {
            // 我方节点在敌方半场 (右下区域)
            if (n->pos.X > 500.f || n->pos.Y > 350.f) s.myControl += 1.f;
        } else {
            if (n->pos.X < 500.f || n->pos.Y < 350.f) s.enControl += 1.f;
        }
    }

    s.enScoreAhead = (s.enScore > s.myScore + 3);
    return s;
}

// 玩家行动学习 (0扩张 1强化 2攻击 3额外行动)
void AI::observePlayerAction(int type) {
    m_playerHistory.push_back(type);
    if (m_playerHistory.size() > 40) m_playerHistory.erase(m_playerHistory.begin());
    m_roundsSeen++;
}

// ===== 数据驱动学习: 从对局文件分析胜方行为 + 方位 =====
void AI::learnFromReplayFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[256];
    std::string winner = "red";
    struct A { int team = 0, type = 0, scB = 0, scA = 0, nk = 0, ek = 0;
               float tx = 0.f, ty = 0.f; };
    std::vector<A> acts;
    A cur; bool inAct = false;
    while (fgets(line, sizeof line, f)) {
        char key[32], val[160];
        if (sscanf(line, "%31s", key) == 1 && strcmp(key, "[A]") == 0) {
            if (inAct) acts.push_back(cur);
            cur = A{}; inAct = true; continue;
        }
        if (sscanf(line, "%31[^=]=%159s", key, val) == 2) {
            if (strcmp(key, "winner") == 0) winner = val;
            else if (strcmp(key, "tm") == 0) cur.team = atoi(val);
            else if (strcmp(key, "ty") == 0) cur.type = atoi(val);
            else if (strcmp(key, "b") == 0) cur.scB = atoi(val);
            else if (strcmp(key, "a") == 0) cur.scA = atoi(val);
            else if (strcmp(key, "nk") == 0) cur.nk = atoi(val);
            else if (strcmp(key, "ek") == 0) cur.ek = atoi(val);
            else if (strcmp(key, "tx") == 0) cur.tx = (float)atof(val);
            else if (strcmp(key, "ty2") == 0) cur.ty = (float)atof(val);
        }
    }
    if (inAct) acts.push_back(cur);
    fclose(f);

    // 统计胜方行为特征 + 方位特征
    int winTeam = (winner == "red") ? 0 : 1;
    int atk = 0, def = 0, col = 0, total = 0;
    // 方位: 胜方平均进攻方向 (相对胜方根的单位向量) + 进攻深度
    PointF myR = winTeam==0 ? PointF{80.f,80.f} : PointF{920.f,620.f};
    PointF enR = winTeam==0 ? PointF{920.f,620.f} : PointF{80.f,80.f};
    float dirX=0.f, dirY=0.f; int dirN=0;
    int deep=0, branchN=0;   // 深入敌半场的分支数
    for (auto& a : acts) {
        if (a.team != winTeam) continue;
        total++;
        if (a.type == 0) {
            if (a.nk + a.ek > 0) atk++;
            else def++;
            // 方位: 目标相对胜方根的方向
            if (a.tx > 1.f || a.ty > 1.f) {
                PointF v{a.tx-myR.X, a.ty-myR.Y};
                float L = std::sqrt(v.X*v.X+v.Y*v.Y);
                if (L > 1.f) { dirX += v.X/L; dirY += v.Y/L; dirN++; }
                // 进攻深度: 目标更接近敌根 → 深入
                branchN++;
                if (len2({a.tx,a.ty}, enR) < len2({a.tx,a.ty}, myR)) deep++;
            }
        } else if (a.type == 1) def++;
        if (a.scA > a.scB) col++;
    }
    if (total > 0) {
        float w = 1.f / (m_gamesLearned + 1);   // 渐进加权平均
        m_learnAggro    = m_learnAggro*(1-w) + ((float)atk/total)*w;
        m_learnDefense  = m_learnDefense*(1-w) + ((float)def/total)*w;
        m_learnCollect  = m_learnCollect*(1-w) + ((float)col/total)*w;
        // 方位: 平均进攻方向 (单位向量)
        if (dirN > 0) {
            float L = std::sqrt(dirX*dirX + dirY*dirY);
            if (L > 0.001f) { dirX/=L; dirY/=L; }
            m_learnDirX = m_learnDirX*(1-w) + dirX*w;
            m_learnDirY = m_learnDirY*(1-w) + dirY*w;
        }
        // 进攻深度: 深入敌半场比例
        if (branchN > 0)
            m_learnDepth = m_learnDepth*(1-w) + ((float)deep/branchN)*w;
        m_gamesLearned++;
    }
}

// 扫描当前目录所有 .btb / .btbdt (Windows)
void AI::learnFromReplayFiles() {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*.btb", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            learnFromReplayFile(fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    // 兼容旧格式
    h = FindFirstFileA("*.btbdt", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            learnFromReplayFile(fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}

// ===== 参数/学习数据持久化 (ai_*.dat) =====
bool AI::saveToFile(const char* path) const {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "# Binary Tree Battle AI config (auto-generated)\n");
    fprintf(f, "[config]\n");
#define CFG(n) fprintf(f, "%s=%.5f\n", #n, m_cfg.n)
    CFG(nodeHit); CFG(hubFactor); CFG(edgeKill); CFG(edgeHit);
    CFG(decisive2); CFG(combo); CFG(strBonus); CFG(advance);
    CFG(collect); CFG(collectLow); CFG(expand); CFG(center); CFG(defense);
    CFG(reserveBase); CFG(spendOpen); CFG(spendMid); CFG(spendTight);
    CFG(extraThresh); CFG(threatMul);
    CFG(reinfBudget); CFG(reinfThreat); CFG(sprintDist); CFG(sprintBonus);
    CFG(extraSprint); CFG(deepPush); CFG(hubPreference); CFG(riskTaker);
#undef CFG
    fprintf(f, "[learned]\n");
#define LRN(n) fprintf(f, "%s=%.5f\n", #n, n)
    LRN(m_learnAggro); LRN(m_learnDefense); LRN(m_learnCollect);
    LRN(m_learnDirX); LRN(m_learnDirY); LRN(m_learnDepth);
    LRN(m_learnSpend); LRN(m_learnPush); LRN(m_learnScorePref);
#undef LRN
    fprintf(f, "games_learned=%d\n", m_gamesLearned);
    fclose(f);
    return true;
}

bool AI::loadFromFile(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[256];
    std::map<std::string, float> vals;
    while (fgets(line, sizeof line, f)) {
        char key[64] = {0}, val[64] = {0};
        if (sscanf(line, " %63[^= \t\r\n]=%63s", key, val) == 2)
            vals[key] = (float)atof(val);
    }
    fclose(f);
    if (vals.empty()) return false;
#define CFG(n) if (vals.count(#n)) m_cfg.n = vals[#n]
    CFG(nodeHit); CFG(hubFactor); CFG(edgeKill); CFG(edgeHit);
    CFG(decisive2); CFG(combo); CFG(strBonus); CFG(advance);
    CFG(collect); CFG(collectLow); CFG(expand); CFG(center); CFG(defense);
    CFG(reserveBase); CFG(spendOpen); CFG(spendMid); CFG(spendTight);
    CFG(extraThresh); CFG(threatMul);
    CFG(reinfBudget); CFG(reinfThreat); CFG(sprintDist); CFG(sprintBonus);
    CFG(extraSprint); CFG(deepPush); CFG(hubPreference); CFG(riskTaker);
#undef CFG
#define LRN(n) if (vals.count(#n)) n = vals[#n]
    LRN(m_learnAggro); LRN(m_learnDefense); LRN(m_learnCollect);
    LRN(m_learnDirX); LRN(m_learnDirY); LRN(m_learnDepth);
    LRN(m_learnSpend); LRN(m_learnPush); LRN(m_learnScorePref);
#undef LRN
    if (vals.count("games_learned")) m_gamesLearned = (int)vals["games_learned"];
    // 边界保护: 学习特征是 0..1 的倾向
    m_learnAggro    = std::max(0.f, std::min(1.f, m_learnAggro));
    m_learnDefense  = std::max(0.f, std::min(1.f, m_learnDefense));
    m_learnCollect  = std::max(0.f, std::min(1.f, m_learnCollect));
    m_learnDepth    = std::max(0.f, std::min(1.f, m_learnDepth));
    m_learnSpend    = std::max(0.f, std::min(1.f, m_learnSpend));
    m_learnPush     = std::max(0.f, std::min(1.f, m_learnPush));
    return true;
}

// ===== 前向威胁检查 =====
float AI::threatPenalty(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                        const Color& enemyTeam, Node* parent, PointF tgt) {
    float pen = 0.f;
    const float risk = m_cfg.riskTaker;   // 冒险度: >1更谨慎(惩罚重), <1更激进
    // 0) 贴近敌根且非杀根的落点 = 敌方主场, 建了也会被拆 → 重罚
    {
        float dEn = len2(tgt, enemyRoot->pos);
        if (dEn < 90.f) pen += (90.f - dEn) * 2.2f;
        if (dEn < 45.f) pen += (45.f - dEn) * 3.0f;
    }
    for (auto* e : nodes) {
        if (!sameColor(e->team, enemyTeam) || e->children.size() >= 2) continue;
        float d = len2(e->pos, tgt);
        if (d < 20.f || d > MAX_D + 3 * EXTRA_D) continue;
        int neededExt = (d > MAX_D) ? (int)std::ceil((d - MAX_D) / EXTRA_D) : 0;
        if (neededExt > 2) continue;
        bool blocked = false;
        for (auto* o : nodes) {
            if (sameColor(o->team, enemyTeam)) continue;
            if (o == parent) continue;
            if (ptSegDist(o->pos, e->pos, tgt) < OCCUPY_R * 0.8f) { blocked = true; break; }
        }
        if (!blocked) pen += 14.f * m_cfg.threatMul;
    }
    for (auto* e : nodes) {
        if (!sameColor(e->team, enemyTeam)) continue;
        float d = len2(e->pos, tgt);
        if (d < 35.f) pen += (35.f - d) * 0.5f * m_cfg.threatMul;
        if (d < 55.f && e->children.size() < 2) pen += (55.f - d) * 0.2f * m_cfg.threatMul;
    }
    for (auto* e : nodes) {
        if (!sameColor(e->team, enemyTeam) || e->children.size() >= 2) continue;
        float d = len2(e->pos, parent->pos);
        if (d < 20.f || d > MAX_D + EXTRA_D) continue;
        PointF mid = {(parent->pos.X + tgt.X) * 0.5f, (parent->pos.Y + tgt.Y) * 0.5f};
        PointF dir = mid - e->pos;
        float L = std::sqrt(dir.X * dir.X + dir.Y * dir.Y);
        if (L < 1.f) continue;
        dir.X /= L; dir.Y /= L;
        for (float dist : {60.f, 90.f, 120.f, 150.f, 180.f, 210.f, 240.f}) {
            PointF ep{e->pos.X + dir.X * dist, e->pos.Y + dir.Y * dist};
            if (segCross(e->pos, ep, parent->pos, tgt)) { pen += 10.f * m_cfg.threatMul; break; }
        }
    }
    float dRoot = len2(tgt, myRoot->pos);
    if (dRoot > 450.f) pen += (dRoot - 450.f) * 0.1f;
    return pen;
}

// ===== 动态评分 =====
float AI::scoreTarget(Node* parent, PointF tgt, int str, int ext,
                       const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                       const std::vector<ScorePoint>& scores, const Color& myTeam,
                       float spendMult) {
    // 动态策略修正 (随局面变化)
    float dynAtk = 1.f, dynExp = 1.f, dynDef = 1.f, dynCol = 1.f;
    if (m_sit.playerAggression > 0.55f) dynDef = 1.6f;          // 玩家凶狠 → 强化防守
    if (m_sit.enScore > m_sit.myScore + 4) dynAtk = 1.3f;      // 敌方积分领先 → 打乱节奏
    if (m_sit.myNodes < m_sit.enNodes) dynExp = 1.5f;           // 版图落后 → 扩张
    if (m_sit.enAvgEdge > 2.2f) dynAtk *= 1.2f;                 // 敌方重防 → 绕/突破
    if (m_sit.playerTurtle) dynAtk *= 1.25f;                    // 玩家龟缩 → 逼战
    if (m_sit.enFrontCount > m_sit.myNodes) dynAtk *= 1.1f;     // 敌方前线多 → 反击
    // 绕后威胁 → 加强防守纵深 (预防敌方偷袭根部)
    if (m_sit.flankThreat) dynDef *= 2.0f;
    // 敌方积分充足 → 重点防御加固 (敌方有能力强攻/绕后/连续行动)
    if (m_sit.enScore >= 8) dynDef *= 1.7f;
    if (m_sit.enScore >= 12) dynDef *= 2.2f;
    // 敌方积分多且我方防守弱 → 优先补防而非冒进
    if (m_sit.enScore >= 10 && m_sit.myAvgEdge < m_sit.enAvgEdge) dynAtk *= 0.8f;
    // 双方未交锋 → 专注收集高分点, 降低攻击 (积攒资源)
    if (m_sit.noContact) { dynCol *= 2.5f; dynAtk *= 0.7f; }
    // 己方积分不足 → 重收集 (后期避免分数流失)
    if (m_sit.myScore < 6) dynCol *= 2.2f;
    // 敌方积分领先 → 收集追赶
    if (m_sit.enScore > m_sit.myScore) dynCol *= 1.8f;
    // 己方积分充裕但敌方更多 → 也倾向收集 (保持资源领先)
    if (m_sit.myScore < 10 && m_sit.enScore >= 8) dynCol *= 1.5f;
    // ===== 换位思考: 我方防线暴露于玩家 → 抑制冒进进攻, 优先补防 =====
    if (m_sit.myWeakEdges >= 2) dynAtk *= 0.85f;
    if (m_sit.myWeakEdges >= 4) dynAtk *= 0.7f;
    if (m_sit.myFrontExposed >= 3) dynAtk *= 0.85f;
    if (m_sit.myWeakEdges >= 3 || m_sit.myFrontExposed >= 4) dynDef *= 1.5f;

    // ===== 全局观察: 黄点资源劣势 → 重收集追赶 =====
    if (m_sit.scorePtAdv < 0) dynCol *= 1.6f;
    if (m_sit.scorePtAdv >= 4) dynCol *= 1.3f;    // 有优势也要守住

    // ===== 全局观察: 威胁走廊 (敌方逼近我根) → 强化防守 =====
    if (m_sit.enNearRoot >= 1) dynDef *= 1.4f;
    if (m_sit.enNearRoot >= 3) dynDef *= 1.8f;

    // ===== 全局观察: 前线对比 (我方前线弱 → 先稳住防线) =====
    if (m_sit.enFrontN > m_sit.myFrontN + 2) dynAtk *= 0.85f;
    if (m_sit.myFrontN == 0 && m_sit.enFrontN > 0) dynDef *= 1.5f;
    if (m_sit.enFrontEdge > m_sit.myFrontEdge * 1.5f) dynDef *= 1.3f;

    // ===== 全局观察: 半场控制 (敌方深入我方半场 → 防守) =====
    if (m_sit.enControl > m_sit.myControl + 2) dynDef *= 1.5f;

    // ===== 全局观察: 敌方积分领先 → 更珍惜积分, 优先收集 =====
    if (m_sit.enScoreAhead) dynCol *= 1.4f;

    // ===== 数据驱动学习: 从对局文件学到的胜方行为特征 (随机应变) =====
    float learnDirBonus = 0.f;
    if (m_gamesLearned > 0) {
        dynAtk *= (0.7f + m_learnAggro * 0.6f);      // 胜方进攻性强 → 学习进攻
        dynDef *= (0.7f + m_learnDefense * 0.6f);    // 胜方重防守 → 学习防守
        dynCol *= (0.7f + m_learnCollect * 0.6f);    // 胜方重收集 → 学习收集
        // 胜方进攻深度 → 更敢深入敌半场
        if (m_learnDepth > 0.6f) dynAtk *= 1.15f;
        // 方位偏好: 候选目标方向与胜方平均进攻方向一致 → 奖励
        if (m_learnDirX != 0.f || m_learnDirY != 0.f) {
            PointF v{tgt.X - myRoot->pos.X, tgt.Y - myRoot->pos.Y};
            float L = std::sqrt(v.X*v.X + v.Y*v.Y);
            if (L > 1.f) {
                float dot = (v.X/L)*m_learnDirX + (v.Y/L)*m_learnDirY;  // -1~1
                learnDirBonus = dot * 14.f;   // 朝向胜方打法方向加分
            }
        }
    }

    float s = 0.5f + learnDirBonus;
    int cost = ext + (str - DEF_STR);
    PointF src = parent->pos;

    // 绕后补防: 候选落子若在缺口扇区方向建立防线 → 防守奖励
    if (m_sit.flankThreat && m_sit.flankGapAngle >= 0) {
        PointF dv{tgt.X - myRoot->pos.X, tgt.Y - myRoot->pos.Y};
        float dRoot = std::sqrt(dv.X * dv.X + dv.Y * dv.Y);
        if (dRoot > 50.f && dRoot < 240.f) {
            const float PI = 3.14159265f;
            int sec = ((int)std::floor(std::atan2(dv.Y, dv.X) / PI * 4.f) + 8) % 8;
            if (sec == m_sit.flankGapAngle) s += 28.f * dynDef;  // 堵住绕后走廊
        }
    }
    // 主动绕后: 候选朝敌方根缺口扇区推进 → 攻击奖励 (声东击西/侧翼突破)
    if (m_sit.enFlankGapAngle >= 0) {
        PointF dv{tgt.X - enemyRoot->pos.X, tgt.Y - enemyRoot->pos.Y};
        float dEn = std::sqrt(dv.X * dv.X + dv.Y * dv.Y);
        if (dEn > 30.f && dEn < 280.f) {
            const float PI = 3.14159265f;
            int sec = ((int)std::floor(std::atan2(dv.Y, dv.X) / PI * 4.f) + 8) % 8;
            if (sec == m_sit.enFlankGapAngle) s += 22.f * dynAtk;  // 从薄弱侧突进
        }
    }

    bool collected = false;
    float collectValue = 0.f;   // 目标附近黄点总值
    for (auto& sp : scores)
        if (sp.alive && len2(tgt, sp.pos) < 18.f) {
            collected = true;
            collectValue += sp.value;
            s += sp.value * ((spendMult > 3.f) ? m_cfg.collectLow : m_cfg.collect) * dynCol;
            s += sp.value * 5.f;   // 黄点未来积分现值 (鼓励从母节点收集)
        }
    // 收集经济性: 花 ext 延长去够黄点必须物有所值 (黄点值 ≥ 扩展花费)
    if (ext > 0 && collected && collectValue < (float)ext)
        s -= ((float)ext - collectValue) * 20.f * spendMult;   // 重罚入不敷出的收集

    int nodesHit = 0, edgesHit = 0, edgesDead = 0, hubValue = 0;
    bool hitRoot = false;
    for (auto* n : nodes) {
        if (sameColor(n->team, myTeam)) continue;
        if ((n->pos.X == src.X && n->pos.Y == src.Y) ||
            (n->pos.X == tgt.X && n->pos.Y == tgt.Y)) continue;
        if (ptSegDist(n->pos, src, tgt) < NODE_R + ATK_M) {
            if (n->parent == nullptr) { hitRoot = true; break; }
            nodesHit++;
            hubValue += subtreeSize(n);
        }
    }
    if (hitRoot) return 10000.f;

    for (auto* n : nodes) {
        if (sameColor(n->team, myTeam)) continue;
        for (auto& c : n->children)
            if (c && segCross(src, tgt, n->pos, c->pos)) {
                edgesHit++;
                if (c->edgeStrength <= 1) edgesDead++;
            }
    }

    s += nodesHit * m_cfg.nodeHit * dynAtk + hubValue * m_cfg.hubFactor * dynAtk;
    s += edgesDead * m_cfg.edgeKill * dynAtk;
    s += (edgesHit - edgesDead) * m_cfg.edgeHit * dynAtk;

    // 从根节点开新枝: 若无战果则惩罚 (鼓励从已有前线节点深化推进)
    if (parent->parent == nullptr && parent->children.size() >= 1) {
        if (nodesHit == 0 && edgesHit == 0 && !collected)
            s -= 15.f;
    }
    // 从已有子节点深化: 轻微奖励
    else if (parent->parent != nullptr) {
        s += 4.f;
    }

    // 扩展距离效率: 无任何战果(无命中/穿越/收集)的扩展重罚, 避免滥用积分
    if (ext > 0 && nodesHit == 0 && edgesHit == 0 && !collected)
        s -= ext * 10.f * spendMult;

    int kills = nodesHit + edgesDead;
    if (kills >= 2) s += m_cfg.decisive2 * dynAtk;
    else if (kills >= 1 && edgesHit >= 1) s += m_cfg.combo * dynAtk;

    if (nodesHit > 0 || edgesHit > 0)
        s += (str - DEF_STR) * m_cfg.strBonus * dynAtk;

    if (kills >= 1 || hitRoot) s += cost * spendMult * 0.4f;

    float dTo = len2(tgt, enemyRoot->pos);
    float dFrom = len2(parent->pos, enemyRoot->pos);
    s += (dFrom - dTo) * m_cfg.advance * dynAtk;
    // 收集黄点时放宽推进惩罚 (收集是长期投资, 允许暂缓推进)
    if (collected && dTo > dFrom)
        s += (dTo - dFrom) * m_cfg.advance * 0.6f;

    float centerX = 500.f, centerY = 350.f;
    float dC = len2(tgt, {centerX, centerY});
    if (dC < 300.f) s += (300.f - dC) * m_cfg.center;

    int pinned = 0;
    for (auto* n : nodes) {
        if (!sameColor(n->team, myTeam) && n->children.size() < 2) {
            float d = len2(tgt, n->pos);
            if (d < 55.f && d > 20.f) pinned++;
        }
    }
    s += pinned * 8.f * dynAtk;

    float minOwnDist = 1e6f;
    for (auto* n : nodes) {
        if (!sameColor(n->team, myTeam) || n == parent) continue;
        float d = len2(tgt, n->pos);
        if (d < minOwnDist) minOwnDist = d;
    }
    if (minOwnDist > 70.f) s += (minOwnDist - 70.f) * m_cfg.expand * dynExp;

    float dParentToRoot = len2(parent->pos, enemyRoot->pos);
    if (dParentToRoot < 250.f) s += (250.f - dParentToRoot) * 0.08f * dynAtk;

    float dMy = len2(tgt, myRoot->pos);
    if (dMy > 120.f) s += dMy * m_cfg.defense * dynDef;
    // 主动防御: 敌方威胁大时, 在己方根附近建立防御节点有奖励 (堵住进攻走廊)
    if (m_sit.flankThreat && dMy < 150.f) s += 15.f * dynDef;
    // 换位思考: 进攻深入敌方腹地且己方防线暴露 → 玩家会反打丢大量节点 → 重罚
    {
        float dEnemy = len2(tgt, enemyRoot->pos);
        if (dEnemy < 200.f && (m_sit.myWeakEdges >= 2 || m_sit.myFrontExposed >= 3))
            s -= (200.f - dEnemy) * 0.12f * dynDef;
    }

    // 母节点脆弱风险: 从弱边/靠近敌方的母节点扩展, 敌方可切断母节点使整棵子树崩
    float parentRisk = 0.f;
    if (parent->parent) {   // parent 不是根
        if (parent->edgeStrength <= 1) parentRisk += 24.f;         // 母节点边太弱
        else if (parent->edgeStrength == 2) parentRisk += 8.f;
        float dPe = len2(parent->pos, enemyRoot->pos);
        if (dPe < 260.f) parentRisk += (260.f - dPe) * 0.08f;      // 母节点靠近敌方
        // 母节点自身子树大 → 切断损失大
        int pval = subtreeSize(parent);
        if (pval >= 3) parentRisk += (pval - 2) * 4.f;
    }
    s -= parentRisk * dynDef;

    if (m_lastTarget.X > 0) s += len2(tgt, m_lastTarget) * 0.02f;

    if (m_turnsWithoutBranch >= 3) {
        s *= 1.25f;
        if (nodesHit > 0) s *= 1.4f;
    }

    s -= cost * spendMult;
    s += (str - DEF_STR) * 2.5f;
    s += noise();
    return s;
}

// ===== 候选目标生成 =====
void AI::genTargets(Node* parent, Node* myRoot, Node* enemyRoot,
                     const std::vector<Node*>& nodes, const std::vector<ScorePoint>& scores,
                     const Color& myTeam, std::vector<PointF>& out) {
    (void)myRoot;
    PointF dir = enemyRoot->pos - parent->pos;
    float dRoot = std::sqrt(dir.X * dir.X + dir.Y * dir.Y);
    if (dRoot > 1.f) {
        dir.X /= dRoot; dir.Y /= dRoot;
        for (float dist : {35.f, 55.f, 75.f, 95.f, 115.f, 140.f, 165.f, 195.f, 225.f})
            out.push_back({parent->pos.X + dir.X * dist, parent->pos.Y + dir.Y * dist});
        float angles[] = {-0.5f, 0.5f, -0.28f, 0.28f, -0.12f, 0.12f, -0.75f, 0.75f};
        for (float ang : angles) {
            float cs = std::cos(ang), sn = std::sin(ang);
            float rx = dir.X * cs - dir.Y * sn, ry = dir.X * sn + dir.Y * cs;
            for (float dist : {70.f, 110.f, 150.f, 190.f, 230.f})
                out.push_back({parent->pos.X + rx * dist, parent->pos.Y + ry * dist});
        }
    }
    for (auto* n : nodes) {
        if (sameColor(n->team, myTeam)) continue;
        PointF d = n->pos - parent->pos;
        float L = std::sqrt(d.X * d.X + d.Y * d.Y);
        if (L < 25.f || L > MAX_D + 3 * EXTRA_D + 20.f) continue;
        d.X /= L; d.Y /= L;
        out.push_back(n->pos);
        for (float off : {-30.f, 30.f, -45.f, 45.f, -70.f, 70.f})
            out.push_back({n->pos.X - d.Y * off, n->pos.Y + d.X * off});
        if (n->children.size() < 2)
            out.push_back({n->pos.X + d.X * 35.f, n->pos.Y + d.Y * 35.f});
    }
    for (auto* n : nodes) {
        if (sameColor(n->team, myTeam)) continue;
        if (n->parent == nullptr) continue;
        PointF mid = {(n->pos.X + enemyRoot->pos.X) * 0.5f,
                      (n->pos.Y + enemyRoot->pos.Y) * 0.5f};
        if (len2(mid, parent->pos) > 30.f && len2(mid, parent->pos) < MAX_D + 3 * EXTRA_D)
            out.push_back(mid);
    }
    for (auto& sp : scores) {
        if (!sp.alive) continue;
        PointF dsp = sp.pos - parent->pos;
        float Lsp = std::sqrt(dsp.X * dsp.X + dsp.Y * dsp.Y);
        if (Lsp < 18.f || Lsp > MAX_D + 3 * EXTRA_D) continue;   // 放宽近距, 母节点可收集
        out.push_back(sp.pos);
        if (Lsp > 1.f) {
            PointF ud{dsp.X / Lsp, dsp.Y / Lsp};
            out.push_back({sp.pos.X - ud.Y * 18.f, sp.pos.Y + ud.X * 18.f});
            out.push_back({sp.pos.X + ud.Y * 18.f, sp.pos.Y - ud.X * 18.f});
        }
    }
    // 环形扫描 (困难难度更密集 → 全局观察更充分)
    int ringN = (m_difficulty==2) ? 32 : (m_difficulty==0 ? 16 : 24);
    for (int a = 0; a < ringN; ++a) {
        float rad = a * 6.28318f / ringN + noise() * 0.06f;
        PointF ud{std::cos(rad), std::sin(rad)};
        for (float dist : {40.f, 70.f, 100.f, 130.f, 160.f, 200.f})
            out.push_back({parent->pos.X + ud.X * dist, parent->pos.Y + ud.Y * dist});
    }
    if (m_turnsWithoutBranch >= 2) {
        for (int a = 0; a < 16; ++a) {
            float rad = a * 6.28318f / 16.f;
            PointF ud{std::cos(rad), std::sin(rad)};
            for (float dist : {100.f, 150.f, 200.f, 240.f})
                out.push_back({parent->pos.X + ud.X * dist, parent->pos.Y + ud.Y * dist});
        }
    }
}

// ===== 评估单个节点 =====
void AI::evaluateNode(Node* n, const std::vector<Node*>& nodes, Node* myRoot,
                      Node* enemyRoot, int myScore, const std::vector<ScorePoint>& scores,
                      const Color& myTeam) {
    std::vector<PointF> targets;
    genTargets(n, myRoot, enemyRoot, nodes, scores, myTeam, targets);
    std::sort(targets.begin(), targets.end(),
        [](auto& a, auto& b) { return a.X < b.X || (a.X == b.X && a.Y < b.Y); });
    targets.erase(std::unique(targets.begin(), targets.end(),
        [](auto& a, auto& b) { return len2(a, b) < 3.f; }), targets.end());

    float spendMult;
    if (m_sit.myNodes < 4 || (int)nodes.size() < 8) spendMult = m_cfg.spendOpen;
    else if (myScore <= 5) spendMult = m_cfg.spendTight;
    else spendMult = m_cfg.spendMid;
    // ===== 全局观察: 局势紧迫时更省分 =====
    if (m_sit.enScoreAhead) spendMult *= 1.4f;          // 敌方积分领先 → 省
    if (m_sit.scorePtAdv < -3) spendMult *= 1.3f;       // 黄点劣势 → 省
    if (m_sit.enNearRoot >= 2 && myScore < 8) spendMult *= 1.3f;  // 被威胁 → 留分防守
    int reserve = computeReserve(myScore, (int)nodes.size());
    int spendable = std::max(0, myScore - reserve);

    for (auto& t : targets) {
        if (t.X < 25 || t.X > WIN_W - 25 || t.Y < 25 || t.Y > WIN_H - 25) continue;
        float dist = len2(n->pos, t);
        if (dist < 20 || dist > MAX_D + 3 * EXTRA_D) continue;
        bool occ = false;
        for (auto* o : nodes)
            if (o != n && len2(o->pos, t) < OCCUPY_R) { occ = true; break; }
        if (occ) continue;
        int ext = 0;
        if (dist > MAX_D) { ext = (int)std::ceil((dist - MAX_D) / EXTRA_D); if (ext > 3) ext = 3; }
        for (int str = DEF_STR; str <= MAX_STR; ++str) {
            int cost = ext + (str - DEF_STR);
            if (cost > spendable) break;
            float s = scoreTarget(n, t, str, ext, nodes, myRoot, enemyRoot, scores, myTeam, spendMult);
            m_allCands.push_back({s, n, t, str, ext});
            if (s > m_best.score) {
                m_best.parent = n; m_best.target = t;
                m_best.strength = str; m_best.extend = ext; m_best.score = s;
            }
        }
    }
}

void AI::updateHeatmap() {
    if (m_allCands.empty()) { m_lastCands.clear(); return; }
    std::sort(m_allCands.begin(), m_allCands.end(),
        [](auto& a, auto& b) { return a.score > b.score; });
    m_lastCands.clear();
    int nShow = std::min((int)m_allCands.size(), 15);
    for (int i = 0; i < nShow; ++i)
        m_lastCands.push_back({m_allCands[i].tgt, m_allCands[i].score});
}

void AI::finalizeBest(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                      int myScore, int enScore, const std::vector<ScorePoint>& scores,
                      const Color& myTeam) {
    // 强制杀根: 积分预算内可一步命中敌根 → 直接取胜 (简单难度不启用, 留机会给玩家)
    if (m_difficulty != 0) {
        Move killMove;
        if (findKillMove(nodes, myRoot, enemyRoot, myScore, scores, myTeam, killMove)) {
            m_best = killMove;
            m_lastCands.clear();
            m_lastCands.push_back({killMove.target, killMove.score});
            return;
        }
    }
    if (m_allCands.empty()) { m_best = Move{}; return; }
    const Color enemyTeam = sameColor(myTeam, Color(255, 220, 53, 69))
        ? Color(255, 0, 123, 255) : Color(255, 220, 53, 69);
    std::sort(m_allCands.begin(), m_allCands.end(),
        [](auto& a, auto& b) { return a.score > b.score; });
    int topN = std::min((int)m_allCands.size(), 25);
    for (int i = 0; i < topN; ++i)
        m_allCands[i].score -= threatPenalty(nodes, myRoot, enemyRoot, enemyTeam,
                                             m_allCands[i].parent, m_allCands[i].tgt);
    std::sort(m_allCands.begin(), m_allCands.end(),
        [](auto& a, auto& b) { return a.score > b.score; });

    // ===== Alpha-Beta 剪枝搜索 (启发式+得分点预测) =====
    if (m_lookahead) {
        const Color enemyTeam = sameColor(myTeam, Color(255, 220, 53, 69))
            ? Color(255, 0, 123, 255) : Color(255, 220, 53, 69);
        // 按难度调深度/分支: Easy 浅, Normal 中, Hard 深
        int abDepth = (m_difficulty==0) ? 2 : (m_difficulty==2 ? 4 : 3);
        int abBranch = (m_difficulty==2) ? 5 : AB_BRANCH;
        SimState root = cloneGame(nodes, myRoot, enemyRoot, scores, myScore, enScore, myTeam);
        int K = std::min((int)m_allCands.size(), abBranch + 2);
        for (int i = 0; i < K; ++i) {
            auto& c = m_allCands[i];
            SimState child = cloneSim(root);
            Node* p = child.nodeMap[c.parent];
            if (!p) continue;
            simApplyMove(child, p, c.tgt, c.str, c.ext, myTeam);
            if (!child.enRoot) { c.score = 1e9f; continue; }      // 直接杀敌根
            if (!child.myRoot) { c.score = -1e9f; continue; }     // 自杀
            float v = alphaBeta(child, abDepth, -1e18f, 1e18f, enemyTeam, myTeam, abBranch);
            c.score = c.score * 0.4f + v * 0.6f;   // 静态分 + 搜索分
        }
    }
    std::sort(m_allCands.begin(), m_allCands.end(),
        [](auto& a, auto& b) { return a.score > b.score; });

    int pick = 0;
    // 简单难度: 偶尔失误 (~12% 概率选次优), 其余走最优
    if (m_difficulty == 0 && m_allCands.size() > 1 && noise() > 1.5f) pick = 1;
    m_best.parent = m_allCands[pick].parent;
    m_best.target = m_allCands[pick].tgt;
    m_best.strength = m_allCands[pick].str;
    m_best.extend = m_allCands[pick].ext;
    m_best.score = m_allCands[pick].score;
    // 更新热力图 (最终含威胁+模拟)
    m_lastCands.clear();
    int nShow = std::min((int)m_allCands.size(), 15);
    for (int i = 0; i < nShow; ++i)
        m_lastCands.push_back({m_allCands[i].tgt, m_allCands[i].score});
}

// ===== 迭代思考 =====
void AI::beginThink(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                    int myScore, int enScore, const std::vector<ScorePoint>& scores,
                    const Color& myTeam) {
    (void)scores; (void)myTeam;
    // 防御: 根缺失(游戏应已结束)时不思考, 让调用方正常收尾
    if (!myRoot || !enemyRoot) { m_thinking = false; m_best = Move{}; return; }
    m_sit = analyzeSituation(nodes, myRoot, enemyRoot, myScore, enScore, scores);
    m_expandables.clear();
    for (auto* n : nodes)
        if (sameColor(n->team, myRoot->team) && n->children.size() < 2)
            m_expandables.push_back(n);
    // 僵局: 聚焦敌根方向
    if (m_turnsWithoutBranch >= 5 && m_expandables.size() > 4) {
        std::sort(m_expandables.begin(), m_expandables.end(),
            [&](Node* a, Node* b) {
                return len2(a->pos, enemyRoot->pos) < len2(b->pos, enemyRoot->pos);
            });
        m_expandables.resize(4);
    }
    m_allCands.clear();
    m_best = Move{}; m_best.score = -9999.f;
    m_iter = 0;
    m_maxIter = std::max(1, std::min((int)m_expandables.size(), 24));
    m_thinking = true;
}

bool AI::thinkStep(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                   int myScore, int enScore, const std::vector<ScorePoint>& scores,
                   const Color& myTeam) {
    if (!m_thinking) return true;
    int idx = m_iter;
    if (idx < (int)m_expandables.size()) {
        evaluateNode(m_expandables[idx], nodes, myRoot, enemyRoot, myScore, scores, myTeam);
    }
    m_iter++;
    updateHeatmap();
    if (m_iter >= m_maxIter || idx >= (int)m_expandables.size() - 1) {
        finalizeBest(nodes, myRoot, enemyRoot, myScore, enScore, scores, myTeam);
        if (m_best.parent) {
            m_lastTarget = m_best.target;
            m_turnsWithoutBranch = 0;
        } else {
            m_turnsWithoutBranch++;
        }
        m_thinking = false;
        return true;
    }
    return false;
}

// ===== 强化 =====
void AI::reinforce(std::vector<Node*>& nodes, int& myScore, int enScore, const Color& myTeam) {
    struct Candidate { Node* child; float threat; int maxUp; };
    std::vector<Candidate> cands;
    for (auto* n : nodes) {
        if (!sameColor(n->team, myTeam)) continue;
        for (auto& c : n->children) {
            if (c->edgeStrength >= 4) continue;
            float minDist = 1e6f;
            for (auto* en : nodes) {
                if (sameColor(en->team, myTeam)) continue;
                float d = ptSegDist(en->pos, n->pos, c->pos);
                if (d < minDist) minDist = d;
            }
            // 扩大覆盖: 距敌方 <200 的边都纳入; 强度1的薄弱边放宽到 <260
            float range = (c->edgeStrength <= 1) ? 260.f : 200.f;
            if (minDist < range) {
                int val = subtreeSize(c.get());
                float upFactor = 1.f;
                if (c->parent && c->parent->parent &&
                    c->parent->edgeStrength <= c->edgeStrength)
                    upFactor = 0.4f;
                float threat = (range - minDist) * (5 - c->edgeStrength)
                             * (0.2f + val * 0.6f) * upFactor;
                int maxUp = std::min(MAX_STR - c->edgeStrength, myScore);
                if (maxUp > 0) cands.push_back({c.get(), threat, maxUp});
            }
        }
    }
    // 每回合可加固更多条边 (覆盖其他已有薄弱节点)
    int maxReinf = (m_turnsWithoutBranch >= 2) ? 3 : 6;
    std::sort(cands.begin(), cands.end(),
              [](auto& a, auto& b) { return a.threat > b.threat; });
    int myNodeCount = 0;
    for (auto* n : nodes) if (sameColor(n->team, myTeam)) myNodeCount++;
    int reserve = computeReserve(myScore, (int)nodes.size());
    int budget = (myNodeCount < 4) ? 0 : std::max(0, myScore - reserve - 1);
    // 敌方积分充足 → 提高防守预算, 加固关键边 (防强攻/绕后)
    if (enScore >= 8) budget = std::max(budget, std::max(0, myScore - reserve));
    if (enScore >= 12) budget = std::max(budget, std::max(0, myScore - reserve + 1));
    // 主动防御: 存在高威胁薄弱边时, 即使积分紧张也强制留防御预算
    if (!cands.empty()) {
        if (cands[0].threat > 40.f) budget = std::max(budget, 1);
        if (cands[0].threat > 90.f) budget = std::max(budget, 2);
    }
    // 预算绝不允许超过剩余分数 (防止扣成负值导致无法行动)
    budget = std::min(budget, std::max(0, myScore));
    // 每条边至少 +1 (若预算够), 让更多薄弱边得到基础加固
    int count = 0;
    for (auto& cd : cands) {
        if (count >= maxReinf || budget <= 0) break;
        int up = std::min(cd.maxUp, std::min(2, budget));
        if (up > 0 && up <= myScore) { myScore -= up; budget -= up; cd.child->edgeStrength += up; count++; }
    }
}

// ===================== 前向模拟 (围棋式 lookahead) =====================
// 局面评估 (我方视角)
float AI::quickEval(const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                    const Color& myTeam) const {
    float s = 0.f;
    int myN = 0, enN = 0;
    float myEdge = 0.f, enEdge = 0.f;
    float minDist = 1e9f;
    for (auto* n : nodes) {
        if (sameColor(n->team, myTeam)) {
            myN++;
            for (auto& c : n->children) myEdge += c->edgeStrength;
            float d = len2(n->pos, enemyRoot->pos);
            if (d < minDist) minDist = d;
        } else {
            enN++;
            for (auto& c : n->children) enEdge += c->edgeStrength;
        }
    }
    s += (myN - enN) * 18.f;
    s += (myEdge - enEdge) * 1.2f;
    if (minDist < 1e8f) s -= minDist * 0.08f;
    return s;
}

// 模拟用静态评分 (确定性, 无动态修正)
float AI::quickScore(Node* parent, PointF tgt, int str, int ext,
                     const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                     const std::vector<ScorePoint>& scores, const Color& myTeam) const {
    float s = 0.5f;
    int cost = ext + (str - DEF_STR);
    PointF src = parent->pos;
    bool collected = false;
    float collectValue = 0.f;
    for (auto& sp : scores)
        if (sp.alive && len2(tgt, sp.pos) < 18.f) {
            collected = true;
            collectValue += sp.value;
            s += sp.value * m_cfg.collect + sp.value * 5.f;  // 现值奖励
        }
    // 收集经济性 (与 scoreTarget 一致)
    if (ext > 0 && collected && collectValue < (float)ext)
        s -= ((float)ext - collectValue) * 20.f;
    int nodesHit = 0, edgesHit = 0, edgesDead = 0, hubValue = 0;
    bool hitRoot = false;
    for (auto* n : nodes) {
        if (sameColor(n->team, myTeam)) continue;
        if ((n->pos.X == src.X && n->pos.Y == src.Y) ||
            (n->pos.X == tgt.X && n->pos.Y == tgt.Y)) continue;
        if (ptSegDist(n->pos, src, tgt) < NODE_R + ATK_M) {
            if (n->parent == nullptr) { hitRoot = true; break; }
            nodesHit++;
            hubValue += subtreeSize(n);   // 枢纽价值: 摧毁大树更值钱
        }
    }
    if (hitRoot) return 10000.f;
    for (auto* n : nodes) {
        if (sameColor(n->team, myTeam)) continue;
        for (auto& c : n->children)
            if (c && segCross(src, tgt, n->pos, c->pos)) {
                edgesHit++;
                if (c->edgeStrength <= 1) edgesDead++;
            }
    }
    s += nodesHit * m_cfg.nodeHit + hubValue * m_cfg.hubFactor
       + edgesDead * m_cfg.edgeKill + (edgesHit - edgesDead) * m_cfg.edgeHit;
    // 扩展无战果重罚 (与 scoreTarget 一致; collected 已在上面定义)
    if (ext > 0 && nodesHit == 0 && edgesHit == 0 && !collected)
        s -= ext * 18.f;
    float dTo = len2(tgt, enemyRoot->pos), dFrom = len2(parent->pos, enemyRoot->pos);
    s += (dFrom - dTo) * m_cfg.advance;
    s -= cost * 2.f;
    s += (str - DEF_STR) * 2.f;
    return s;
}

// 某节点贪心最佳移动 (模拟用)
AI::Move AI::bestFromNode(Node* n, const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                          int myScore, const std::vector<ScorePoint>& scores, const Color& myTeam) {
    Move best; best.score = -9999.f;
    std::vector<PointF> targets;
    genTargets(n, myRoot, enemyRoot, nodes, scores, myTeam, targets);
    std::sort(targets.begin(), targets.end(),
        [](auto& a, auto& b) { return a.X < b.X || (a.X == b.X && a.Y < b.Y); });
    targets.erase(std::unique(targets.begin(), targets.end(),
        [](auto& a, auto& b) { return len2(a, b) < 3.f; }), targets.end());
    for (auto& t : targets) {
        if (t.X < 25 || t.X > WIN_W - 25 || t.Y < 25 || t.Y > WIN_H - 25) continue;
        float dist = len2(n->pos, t);
        if (dist < 20 || dist > MAX_D + 3 * EXTRA_D) continue;
        bool occ = false;
        for (auto* o : nodes)
            if (o != n && len2(o->pos, t) < OCCUPY_R) { occ = true; break; }
        if (occ) continue;
        int ext = 0;
        if (dist > MAX_D) { ext = (int)std::ceil((dist - MAX_D) / EXTRA_D); if (ext > 3) ext = 3; }
        for (int str = DEF_STR; str <= MAX_STR; ++str) {
            if (ext + (str - DEF_STR) > std::max(0, myScore)) break;
            float s = quickScore(n, t, str, ext, nodes, myRoot, enemyRoot, scores, myTeam);
            if (s > best.score) { best.parent = n; best.target = t;
                                   best.strength = str; best.extend = ext; best.score = s; }
        }
    }
    return best;
}

// 前向模拟: 候选移动后, 模拟玩家应对+我方再应对, 返回局面增益
float AI::simulateLookahead(Node* parent, PointF tgt, int str, int ext,
                            const std::vector<Node*>& nodes, Node* myRoot, Node* enemyRoot,
                            int myScore, int enScore, const std::vector<ScorePoint>& scores,
                            const Color& myTeam) {
    SimState st = cloneGame(nodes, myRoot, enemyRoot, scores, myScore, enScore, myTeam);
    Node* clParent = st.nodeMap[parent];
    if (!clParent || !st.myRoot || !st.enRoot) return 0.f;
    if (!simApplyMove(st, clParent, tgt, str, ext, myTeam)) return 0.f;

    const Color enemyTeam = isRedC(myTeam) ? Color(255, 0, 123, 255) : Color(255, 220, 53, 69);
    // 从某方所有可扩展节点取全局贪心最优
    auto globalBest = [&](const Color& team, Node* myR, Node* enR, int mySc,
                          AI::Move& out) -> bool {
        out = AI::Move{}; out.score = -9999.f;
        for (auto* n : st.all) {
            if (sameColor(n->team, team) && n->children.size() < 2) {
                Move m = bestFromNode(n, st.all, myR, enR, mySc, st.scores, team);
                if (m.score > out.score) out = m;
            }
        }
        return out.parent != nullptr;
    };
    // 2 轮: 玩家应对 → 我方再应对
    for (int d = 0; d < 2; ++d) {
        if (!st.myRoot || !st.enRoot) break;
        Move opp;
        if (globalBest(enemyTeam, st.enRoot, st.myRoot, st.scoreEn, opp))
            simApplyMove(st, opp.parent, opp.target, opp.strength, opp.extend, enemyTeam);
        if (!st.myRoot || !st.enRoot) break;   // 应对后某方根被摧毁
        Move me;
        if (globalBest(myTeam, st.myRoot, st.enRoot, st.scoreMy, me))
            simApplyMove(st, me.parent, me.target, me.strength, me.extend, myTeam);
    }
    // 胜负判定: 敌根被摧毁=我方胜(大正), 我方根被摧毁=我方败(大负)
    if (!st.enRoot) return 1e9f;
    if (!st.myRoot) return -1e9f;
    float base = quickEval(nodes, myRoot, enemyRoot, myTeam);
    float after = quickEval(st.all, st.myRoot, st.enRoot, myTeam);
    return after - base;
}

// ===================== 启发式 + Alpha-Beta 剪枝搜索 =====================

// 启发式局面评估 (含得分点未来价值预测)
float AI::heuristicEval(const SimState& st, const Color& myTeam) const {
    float e = 0.f;
    int myN = 0, enN = 0;
    float myEdge = 0.f, enEdge = 0.f;
    float minMyToEn = 1e9f;
    for (auto* n : st.all) {
        if (sameColor(n->team, myTeam)) {
            myN++;
            for (auto& c : n->children) myEdge += c->edgeStrength;
            if (st.enRoot) {
                float d = len2(n->pos, st.enRoot->pos);
                if (d < minMyToEn) minMyToEn = d;
            }
        } else {
            enN++;
            for (auto& c : n->children) enEdge += c->edgeStrength;
        }
    }
    e += (myN - enN) * 18.f;
    e += (myEdge - enEdge) * 1.2f;
    if (minMyToEn < 1e8f) e -= minMyToEn * 0.08f;

    // 得分点预测: 每颗黄点按"谁更可能收集"折算未来价值
    for (auto& sp : st.scores) {
        if (!sp.alive) continue;
        float myBest = 1e9f, enBest = 1e9f;
        for (auto* n : st.all) {
            if (n->children.size() >= 2) continue;
            float d = len2(n->pos, sp.pos);
            if (sameColor(n->team, myTeam)) { if (d < myBest) myBest = d; }
            else { if (d < enBest) enBest = d; }
        }
        float w = 0.f;
        if (myBest < enBest - 30.f) w = 1.f;
        else if (enBest < myBest - 30.f) w = -1.f;
        else w = (myBest <= enBest) ? 0.5f : -0.5f;
        e += sp.value * w * 6.f;   // 黄点归属倾向影响未来积分
    }
    return e;
}

// 候选缩减: 返回某方 top-K 贪心最优移动 (剪枝用)
std::vector<AI::Move> AI::topMoves(const SimState& st, const Color& team, int K,
                                   const Color& myTeam) {
    std::vector<Move> all;
    Node* myR = sameColor(team, myTeam) ? st.myRoot : st.enRoot;
    Node* enR = sameColor(team, myTeam) ? st.enRoot : st.myRoot;
    int sc = sameColor(team, myTeam) ? st.scoreMy : st.scoreEn;
    for (auto* n : st.all) {
        if (!sameColor(n->team, team) || n->children.size() >= 2) continue;
        Move m = bestFromNode(n, st.all, myR, enR, sc, st.scores, team);
        if (m.parent) all.push_back(m);
    }
    std::sort(all.begin(), all.end(), [](auto& a, auto& b) { return a.score > b.score; });
    if ((int)all.size() > K) all.resize(K);
    return all;
}

// Alpha-Beta 剪枝: 我方最大化, 敌方最小化
float AI::alphaBeta(const SimState& st, int depth, float alpha, float beta,
                    const Color& turn, const Color& myTeam, int branch) {
    if (depth <= 0 || !st.myRoot || !st.enRoot)
        return heuristicEval(st, myTeam);
    Color opp = isRedC(turn) ? Color(255, 0, 123, 255) : Color(255, 220, 53, 69);
    auto moves = topMoves(st, turn, branch, myTeam);
    if (sameColor(turn, myTeam)) {   // 我方回合 → 最大化
        float best = -1e18f;
        for (auto& mv : moves) {
            SimState child = cloneSim(st);
            Node* p = child.nodeMap[mv.parent];
            if (!p) continue;
            simApplyMove(child, p, mv.target, mv.strength, mv.extend, turn);
            float v = alphaBeta(child, depth - 1, alpha, beta, opp, myTeam, branch);
            if (v > best) best = v;
            if (best > alpha) alpha = best;
            if (alpha >= beta) break;   // 剪枝
        }
        return best;
    } else {                            // 敌方回合 → 最小化
        float best = 1e18f;
        for (auto& mv : moves) {
            SimState child = cloneSim(st);
            Node* p = child.nodeMap[mv.parent];
            if (!p) continue;
            simApplyMove(child, p, mv.target, mv.strength, mv.extend, turn);
            float v = alphaBeta(child, depth - 1, alpha, beta, myTeam, myTeam, branch);
            if (v < best) best = v;
            if (best < beta) beta = best;
            if (alpha >= beta) break;   // 剪枝
        }
        return best;
    }
}
