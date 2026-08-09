/**
 * selfplay2.cpp — 自我对弈强度评估器 (独立于参数搜索训练器 selfplay)
 *
 * 让两个 AI 完全自对弈 (公平 10:10), 批量对局并输出:
 *   - 胜率 / 平局率 / 平均回合数 (稳定性与平衡性)
 *   - 行为统计: 收集 / 强化 / 攻击 / 额外行动 / 大步推进 (验证 AI 是否按预期打)
 *   - 每局存档为 .btb (ava_mid_日期_时间.btb, 含 [S] 黄点段, 可回放)
 *
 * 用法:
 *   selfplay2 [games=50] [seedBase=1000] [threads=1]  基本模式
 *   selfplay2 -v [games] [seed]                       verbose 逐局摘要
 */
#include "ai.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <thread>
#include <atomic>
#include <ctime>

using namespace Gdiplus;

const Color CLR_RED(255, 220, 53, 69);
const Color CLR_BLUE(255, 0, 123, 255);

namespace {
    inline float len2(PointF a, PointF b) {
        float dx = a.X - b.X, dy = a.Y - b.Y;
        return std::sqrt(dx * dx + dy * dy);
    }
    inline bool sameColor(const Color& a, const Color& b) { return a.GetValue() == b.GetValue(); }
    inline bool segCross(PointF p1, PointF p2, PointF p3, PointF p4) {
        auto ccw = [](PointF a, PointF b, PointF c) {
            return (c.Y - a.Y) * (b.X - a.X) > (b.Y - a.Y) * (c.X - a.X);
        };
        auto pe = [](PointF a, PointF b) { return a.X == b.X && a.Y == b.Y; };
        if (pe(p1, p3) || pe(p1, p4) || pe(p2, p3) || pe(p2, p4)) return false;
        return ccw(p1, p3, p4) != ccw(p2, p3, p4) && ccw(p1, p2, p3) != ccw(p1, p2, p4);
    }
    inline float ptSegDist(PointF p, PointF a, PointF b) {
        float dx = b.X - a.X, dy = b.Y - a.Y;
        if (dx == 0 && dy == 0) return len2(p, a);
        float t = std::max(0.f, std::min(1.f,
            ((p.X - a.X) * dx + (p.Y - a.Y) * dy) / (dx * dx + dy * dy)));
        return len2(p, {a.X + t * dx, a.Y + t * dy});
    }
}

// ===== 对局 (与主游戏同规则: 黄点收集后补充, 强化, 额外行动) =====
struct Game {
    std::unique_ptr<Node> rRoot, bRoot;
    std::vector<Node*> all;
    std::vector<ScorePoint> scores;
    int rScore = 10, bScore = 10;
    int redCollected = 0, blueCollected = 0;
    int redReinf = 0, blueReinf = 0;
    int redExtra = 0, blueExtra = 0;
    int redBig = 0, blueBig = 0;       // 大手笔 (>2分) 次数
    int redKills = 0, blueKills = 0;
    Color turn = CLR_RED;
    bool over = false;
    int winner = 0;                    // 1=红 2=蓝 0=平
    std::mt19937 m_rng;

    Game(unsigned seed) : m_rng(seed) {
        rRoot = std::make_unique<Node>(Node{{80.f, 80.f}, CLR_RED});
        bRoot = std::make_unique<Node>(Node{{920.f, 620.f}, CLR_BLUE});
        all.push_back(rRoot.get()); all.push_back(bRoot.get());
        for (int i = 0; i < 3; ++i) spawnScore();
    }
    void spawnScore() {
        if ((int)scores.size() >= 5) return;
        std::uniform_real_distribution<float> xd(40, 960), yd(40, 660);
        int vv;
    { int rr = m_rng() % 100; if (rr < 40) vv = 1; else if (rr < 70) vv = 2; else vv = 3; }
        for (int t = 0; t < 50; ++t) {
            PointF p{xd(m_rng), yd(m_rng)};
            bool clash = false;
            for (auto& s : scores) if (s.alive && len2(p, s.pos) < 32) { clash = true; break; }
            if (clash) continue;
            for (auto* n : all) if (len2(p, n->pos) < 30) { clash = true; break; }
            if (!clash) { scores.push_back({p, vv}); return; }
        }
    }
    void collectAt(PointF p, const Color& team) {
        int& sc = sameColor(team, CLR_RED) ? rScore : bScore;
        bool got = false;
        for (auto& sp : scores)
            if (sp.alive && len2(p, sp.pos) < 18.f) {
                sp.alive = false; sc += sp.value;
                if (sameColor(team, CLR_RED)) redCollected++; else blueCollected++;
                got = true;
            }
        if (got) spawnScore();
    }
    Node* findNode(PointF p, const Color& team) {
        for (auto* n : all)
            if (sameColor(n->team, team) && fabs(n->pos.X - p.X) < 1.f && fabs(n->pos.Y - p.Y) < 1.f)
                return n;
        return nullptr;
    }
    void killSubtree(Node* node) {
        if (!node) return;
        if (std::find(all.begin(), all.end(), node) == all.end()) return;
        std::vector<Node*> dead;
        std::function<void(Node*)> sub = [&](Node* r) { dead.push_back(r); for (auto& c : r->children) sub(c.get()); };
        sub(node);
        for (Node* n : dead) all.erase(std::remove(all.begin(), all.end(), n), all.end());
        if (node->parent) {
            auto& sib = node->parent->children;
            sib.erase(std::remove_if(sib.begin(), sib.end(),
                [node](auto& p) { return p.get() == node; }), sib.end());
        } else {
            if (node == rRoot.get()) rRoot.reset();
            if (node == bRoot.get()) bRoot.reset();
        }
    }
    void processAttack(PointF p1, PointF p2, const Color& attacker) {
        std::set<Node*> hitNodes, crossEdges;
        for (auto* n : all) {
            if (sameColor(n->team, attacker)) continue;
            if ((n->pos.X == p1.X && n->pos.Y == p1.Y) || (n->pos.X == p2.X && n->pos.Y == p2.Y)) continue;
            if (ptSegDist(n->pos, p1, p2) < 12.f) hitNodes.insert(n);
        }
        for (auto* n : all) {
            if (sameColor(n->team, attacker)) continue;
            for (auto& c : n->children)
                if (c && segCross(p1, p2, n->pos, c->pos)) crossEdges.insert(c.get());
        }
        // 命中节点本体: 削弱该节点连接父边的强度, 归零 → 整棵子树摧毁
        std::set<Node*> toKill;
        for (Node* t : hitNodes) {
            if (!t->parent) toKill.insert(t);
            else if (--t->edgeStrength <= 0) toKill.insert(t);
        }
        // 穿越边: 削弱线段强度, 归零 → 整棵子树摧毁
        for (Node* c : crossEdges) {
            if (--c->edgeStrength <= 0) toKill.insert(c);
        }
        for (Node* t : toKill) killSubtree(t);
    }
    void checkVictory() {
        bool rA = rRoot && std::find(all.begin(), all.end(), rRoot.get()) != all.end();
        bool bA = bRoot && std::find(all.begin(), all.end(), bRoot.get()) != all.end();
        if (!rA) { over = true; winner = 2; }
        if (!bA) { over = true; winner = 1; }
    }
    // 应用一步移动 (统计行为指标)
    bool applyMove(const AI::Move& mv, const Color& team, int& cost, int& hit) {
        if (!mv.parent || mv.score <= -9000) return false;
        int& sc = sameColor(team, CLR_RED) ? rScore : bScore;
        cost = mv.extend + (mv.strength - 1);
        if (cost > sc) return false;
        sc -= cost;
        if (cost > 2) {
            if (sameColor(team, CLR_RED)) redBig++; else blueBig++;
        }
        auto nd = std::make_unique<Node>();
        nd->pos = mv.target; nd->team = team; nd->parent = mv.parent;
        nd->edgeStrength = mv.strength;
        Node* raw = nd.get();
        mv.parent->children.push_back(std::move(nd));
        all.push_back(raw);
        collectAt(mv.target, team);
        int before = (int)all.size();
        processAttack(mv.parent->pos, mv.target, team);
        hit = before - (int)all.size();
        if (sameColor(team, CLR_RED)) redKills += hit; else blueKills += hit;
        checkVictory();
        return true;
    }
    void endTurn() {
        turn = sameColor(turn, CLR_RED) ? CLR_BLUE : CLR_RED;
        scores.erase(std::remove_if(scores.begin(), scores.end(),
            [](auto& s) { return !s.alive; }), scores.end());
    }
};

// 完整思考 (含 reinforce, 与主游戏一致)
static AI::Move thinkMove(AI& ai, Game& g, const Color& team) {
    Node* myRoot = sameColor(team, CLR_RED) ? g.rRoot.get() : g.bRoot.get();
    Node* enRoot = sameColor(team, CLR_RED) ? g.bRoot.get() : g.rRoot.get();
    int& mySc = sameColor(team, CLR_RED) ? g.rScore : g.bScore;
    int enSc = sameColor(team, CLR_RED) ? g.bScore : g.rScore;
    // 先强化 (主游戏 startThink 的行为); 记录实际花费的边数
    int scBefore = mySc;
    ai.reinforce(g.all, mySc, enSc, team);
    int cost = scBefore - mySc;
    if (sameColor(team, CLR_RED)) g.redReinf += cost; else g.blueReinf += cost;
    ai.setLookahead(false);   // 快速批量
    ai.beginThink(g.all, myRoot, enRoot, mySc, enSc, g.scores, team);
    while (ai.thinking())
        ai.thinkStep(g.all, myRoot, enRoot, mySc, enSc, g.scores, team);
    return ai.result();
}

// 一局对弈; 返回获胜方 1=红 2=蓝 0=平
static int playGame(unsigned seed, Game* outG, bool* outHasReinf, int* outReinfCost) {
    Game g(seed);
    AI aiR, aiB;
    aiR.setDifficulty(2); aiB.setDifficulty(2);
    const int MAX_TURNS = 300;
    int turns = 0;
    while (!g.over && turns < MAX_TURNS) {
        if (sameColor(g.turn, CLR_RED)) {
            auto mv = thinkMove(aiR, g, CLR_RED);
            int cost, hit;
            g.applyMove(mv, CLR_RED, cost, hit);
            // 额外行动 (冲刺区或高价值; 第二步仅杀根/冲刺推进才执行)
            if (!g.over && g.rScore >= 3) {
                Node* enR = g.bRoot.get();
                float dT = (mv.parent && enR) ? len2(mv.target, enR->pos) : 1e9f;
                if (mv.score > 500.f || dT < 240.f) {
                    g.rScore -= 3;
                    auto mv2 = thinkMove(aiR, g, CLR_RED);
                    bool doIt = (mv2.parent && (mv2.score > 1e8f ||
                        (mv2.score > 500.f && enR && len2(mv2.target, enR->pos) < 220.f)));
                    if (doIt) { int c2, h2; g.applyMove(mv2, CLR_RED, c2, h2); g.redExtra++; }
                    else g.rScore += 3;
                }
            }
        } else {
            auto mv = thinkMove(aiB, g, CLR_BLUE);
            int cost, hit;
            g.applyMove(mv, CLR_BLUE, cost, hit);
            if (!g.over && g.bScore >= 3) {
                Node* enR = g.rRoot.get();
                float dT = (mv.parent && enR) ? len2(mv.target, enR->pos) : 1e9f;
                if (mv.score > 500.f || dT < 240.f) {
                    g.bScore -= 3;
                    auto mv2 = thinkMove(aiB, g, CLR_BLUE);
                    bool doIt = (mv2.parent && (mv2.score > 1e8f ||
                        (mv2.score > 500.f && enR && len2(mv2.target, enR->pos) < 220.f)));
                    if (doIt) { int c2, h2; g.applyMove(mv2, CLR_BLUE, c2, h2); g.blueExtra++; }
                    else g.bScore += 3;
                }
            }
        }
        g.endTurn(); turns++;
    }
    if (outG) *outG = std::move(g);
    return g.over ? g.winner : 0;
}

// 保存对局为 .btb (ava_mid_日期_时间.btb, 含 [S] 黄点段)
static void saveGame(const Game& g, const std::string& tag) {
    time_t t = time(nullptr);
    struct tm tmv{}; localtime_s(&tmv, &t);
    char fn[160];
    snprintf(fn, sizeof fn, "ava_mid_%04d%02d%02d_%02d%02d%02d_%s.btb",
        tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec, tag.c_str());
    FILE* f = nullptr;
    if (fopen_s(&f, fn, "w") != 0 || !f) return;
    fprintf(f, "BTBDT1\n");
    fprintf(f, "red_score=%d\n", g.rScore);
    fprintf(f, "blue_score=%d\n", g.bScore);
    fprintf(f, "difficulty=1\n");
    fprintf(f, "winner=%s\n", g.winner == 1 ? "red" : (g.winner == 2 ? "blue" : "draw"));
    // 黄点: 已收集的无法还原初始布局, 记录存活黄点 (供参考)
    int nSp = 0;
    for (auto& sp : g.scores) if (sp.alive) nSp++;
    fprintf(f, "scores=%d\n", nSp);
    for (auto& sp : g.scores) {
        if (!sp.alive) continue;
        fprintf(f, "[S]\n");
        fprintf(f, "x=%.1f\n", sp.pos.X);
        fprintf(f, "y=%.1f\n", sp.pos.Y);
        fprintf(f, "v=%d\n", sp.value);
    }
    // 简化行动记录: 用当前局面导出节点 (非完整对局, 供研究)
    for (auto* n : g.all) {
        if (!n->parent) continue;
        fprintf(f, "[A]\n");
        fprintf(f, "t=0\n");
        fprintf(f, "tm=%d\n", sameColor(n->team, CLR_RED) ? 0 : 1);
        fprintf(f, "ty=0\n");
        fprintf(f, "px=%.1f\n", n->parent->pos.X);
        fprintf(f, "py=%.1f\n", n->parent->pos.Y);
        fprintf(f, "tx=%.1f\n", n->pos.X);
        fprintf(f, "ty2=%.1f\n", n->pos.Y);
        fprintf(f, "s=%d\n", n->edgeStrength);
        fprintf(f, "e=0\n");
        fprintf(f, "b=0\n");
        fprintf(f, "a=0\n");
        fprintf(f, "nk=0\n");
        fprintf(f, "ek=0\n");
    }
    fclose(f);
}

// 汇总统计
struct Stats {
    int redW = 0, blueW = 0, draws = 0;
    long long turns = 0;
    long long collect[2] = {0,0}, reinf[2] = {0,0}, extra[2] = {0,0};
    long long big[2] = {0,0}, kills[2] = {0,0};
};

int main(int argc, char** argv) {
    int games = 50, seedBase = 1000, threads = 1;
    bool verbose = false;
    {
        int pos = 0;
        for (int i = 1; i < argc; ++i) {
            if (argv[i][0] == '-') { if (strcmp(argv[i], "-v") == 0) verbose = true; continue; }
            if (pos == 0) games = atoi(argv[i]);
            else if (pos == 1) seedBase = atoi(argv[i]);
            else if (pos == 2) threads = atoi(argv[i]);
            pos++;
        }
        if (games < 1) games = 1;
        if (threads < 1) threads = 1;
    }

    printf("=== Self-play Evaluation (fair 10:10) ===\n");
    printf("Games: %d  SeedBase: %d  Threads: %d\n\n", games, seedBase, threads);
    fflush(stdout);

    Stats st;
    std::atomic<int> done{0};
    std::vector<std::unique_ptr<Game>> results(games);
    std::vector<int> winn(games);
    std::vector<std::thread> pool;
    int per = games / threads;
    for (int th = 0; th < threads; ++th) {
        pool.emplace_back([&, th]() {
            int start = th * per, end = (th == threads - 1) ? games : start + per;
            for (int i = start; i < end; ++i) {
                int seed = seedBase + i;
                auto g = std::make_unique<Game>(0);
                winn[i] = playGame(seed, g.get(), nullptr, nullptr);
                results[i] = std::move(g);
                done.fetch_add(1, std::memory_order_relaxed);
                if (verbose) {
                    printf("  game %3d: %s (%d turns, collect R%d/B%d, reinf R%d/B%d, extra R%d/B%d)\n",
                        i + 1, winn[i] == 1 ? "RED" : (winn[i] == 2 ? "BLUE" : "draw"),
                        (int)results[i]->all.size() / 2,  // 近似回合
                        results[i]->redCollected, results[i]->blueCollected,
                        results[i]->redReinf, results[i]->blueReinf,
                        results[i]->redExtra, results[i]->blueExtra);
                }
            }
        });
    }
    for (auto& th : pool) th.join();

    // 汇总
    for (int i = 0; i < games; ++i) {
        Game& g = *results[i];
        if (winn[i] == 1) st.redW++; else if (winn[i] == 2) st.blueW++; else st.draws++;
        st.turns += (int)g.all.size() / 2;
        st.collect[0] += g.redCollected; st.collect[1] += g.blueCollected;
        st.reinf[0] += g.redReinf; st.reinf[1] += g.blueReinf;
        st.extra[0] += g.redExtra; st.extra[1] += g.blueExtra;
        st.big[0] += g.redBig; st.big[1] += g.blueBig;
        st.kills[0] += g.redKills; st.kills[1] += g.blueKills;
    }
    int t = games ? (int)(st.turns / games) : 0;

    printf("\n========== RESULT (%d games) ==========\n", games);
    printf("  RED  %d W  |  BLUE %d W  |  %d draws  (avg %d turns)\n",
           st.redW, st.blueW, st.draws, t);
    float redRate = games ? 100.f * (st.redW + st.draws * 0.5f) / games : 50.f;
    printf("  Red win-rate (draw=0.5): %.1f%%\n", redRate);
    printf("\n-- Behavior (avg per side per game) --\n");
    printf("  Collect : Red %.1f  Blue %.1f\n", (float)st.collect[0]/games, (float)st.collect[1]/games);
    printf("  Reinforce: Red %.1f  Blue %.1f\n", (float)st.reinf[0]/games, (float)st.reinf[1]/games);
    printf("  Extra   : Red %.1f  Blue %.1f\n", (float)st.extra[0]/games, (float)st.extra[1]/games);
    printf("  BigMoves(>2pts): Red %.1f  Blue %.1f\n", (float)st.big[0]/games, (float)st.big[1]/games);
    printf("  Kills   : Red %.1f  Blue %.1f\n", (float)st.kills[0]/games, (float)st.kills[1]/games);

    // 保存最后5局 (供回放研究)
    int saveN = std::min(5, games);
    for (int i = games - saveN; i < games; ++i)
        saveGame(*results[i], "game" + std::to_string(i + 1));
    printf("\nSaved last %d games as ava_mid_*.btb (replayable)\n", saveN);
    return 0;
}
