/* =====================================================================
 *  示例 AI 插件 —— 演示 ai_plugin.h 的完整用法
 *  ---------------------------------------------------------------------
 *  策略（很朴素，供参考）：
 *    1) 开局附近有薄弱前线边且积分宽裕 → 先强化该边（AI_ACT_REINF_EDGE）
 *    2) 否则：从「离敌根最近且还能扩展的己方节点」朝敌根方向伸分支，
 *       顺路靠近得分点，避开敌方节点；敌根在近距时用扩展/买额外行动冲刺
 *  ---------------------------------------------------------------------
 *  编译：
 *    MSVC : cl /LD /O2 /std:c++17 /DAI_PLUGIN_BUILD sample_plugin.cpp
 *    g++  : g++ -O2 -shared -static -static-libgcc -static-libstdc++
 *           -DAI_PLUGIN_BUILD sample_plugin.cpp -o sample_ai.dll
 *  产物丢进游戏目录的 ai_plugins\ 即可在 vs AI 难度菜单里选中。
 * ===================================================================== */

#include "ai_plugin.h"
#include <cmath>
#include <cstring>

static float sq(float v){ return v*v; }
static float dist2(float ax,float ay,float bx,float by){ return std::sqrt(sq(ax-bx)+sq(ay-by)); }

extern "C" {

AI_PLUGIN_EXPORT const char* aiPluginName(void){ return "Sample Advance AI"; }
AI_PLUGIN_EXPORT const char* aiPluginAuthor(void){ return "CherryClaw"; }
AI_PLUGIN_EXPORT int aiPluginApiVersion(void){ return AI_PLUGIN_API_VERSION; }
AI_PLUGIN_EXPORT void aiPluginInit(void){ /* 可选：加载时初始化 */ }
AI_PLUGIN_EXPORT void aiPluginShutdown(void){ /* 可选：卸载时清理 */ }
AI_PLUGIN_EXPORT void aiPluginThinkStart(const AIPluginState*){ /* 可选：每回合决策前 */ }

static int myScore(const AIPluginState* st){
    return st->myTeam==0 ? st->redScore : st->blueScore;
}

AI_PLUGIN_EXPORT int aiPluginGetMove(const AIPluginState* st, AIPluginMove* out){
    if(!st || !out || st->apiVersion != AI_PLUGIN_API_VERSION) return 0;
    std::memset(out, 0, sizeof *out);

    /* --- 找双方根节点 --- */
    int myRoot=-1, enRoot=-1;
    for(int i=0;i<st->nodeCount;i++){
        if(st->nodes[i].parentId<0){
            if(st->nodes[i].team==st->myTeam) myRoot=i;
            else enRoot=i;
        }
    }
    if(enRoot<0 || myRoot<0) return 1;              // 局面异常，跳过
    const AIPluginNode& en = st->nodes[enRoot];

    /* --- 可选：先强化一条薄弱前线边（演示 REINF_EDGE） --- */
    for(int i=0;i<st->nodeCount;i++){
        const AIPluginNode& c = st->nodes[i];
        if(c.team!=st->myTeam || c.isolated || c.parentId<0) continue;
        if(c.edgeStrength>1) continue;              // 只考虑强度 1 的薄弱边
        const AIPluginNode& p = st->nodes[c.parentId];
        float mx=(p.x+c.x)*0.5f, my=(p.y+c.y)*0.5f;
        if(dist2(mx,my,en.x,en.y) < 200.f && myScore(st)>=6){
            out->valid=1; out->action=AI_ACT_REINF_EDGE;
            out->targetId=i; out->strength=2;        // 强化到 2
            return 1;
        }
    }

    /* --- 主策略：朝敌根推进（演示 BRANCH + extend + buyExtra） --- */
    auto collectValue = [&](float x,float y)->float{
        float v=0.f;
        for(int i=0;i<st->scorePointCount;i++){
            const AIPluginScorePoint& sp=st->scorePoints[i];
            if(!sp.alive) continue;
            float d=dist2(x,y,sp.x,sp.y);
            if(d<60.f) v += sp.value*6.f*(1.f-d/60.f);   // 顺路收集加分
        }
        return v;
    };
    auto evaluate = [&](const AIPluginNode& p,float tx,float ty,int ext,int str)->float{
        float dToEn = dist2(tx,ty,en.x,en.y);
        float cost  = (float)(ext+(str-1));
        if(cost > myScore(st)) return -1e9f;
        if(myScore(st)-(int)cost < 3) ;              // 留保底(软惩罚下方统一处理)
        float s = -dToEn*0.8f - dist2(tx,ty,p.x,p.y)*0.2f + collectValue(tx,ty);
        for(int i=0;i<st->nodeCount;i++){
            const AIPluginNode& n=st->nodes[i];
            if(n.team==st->myTeam) continue;
            float d=dist2(tx,ty,n.x,n.y);
            if(d < st->occupyRadius+2.f) return -1e9f;   // 落点会被占
            if(d < 80.f) s -= (80.f-d)*0.3f;             // 靠近敌方惩罚
        }
        if(myScore(st)-(int)cost < 3) s -= 40.f;         // 保底惩罚
        return s;
    };

    int bestP=-1; float bestX=0,bestY=0; int bestExt=0,bestStr=1; float bestS=-1e9f;
    const float baseMax = st->maxBranchLength - 3.f*40.f;   // 基础 120px

    for(int i=0;i<st->nodeCount;i++){
        const AIPluginNode& p=st->nodes[i];
        if(p.team!=st->myTeam || p.isolated || p.childCount>=2) continue;
        float dx=en.x-p.x, dy=en.y-p.y;
        float pd=std::sqrt(sq(dx)+sq(dy));
        if(pd<0.001f) continue;
        float ux=dx/pd, uy=dy/pd;
        for(int ext=0;ext<=3;ext++){
            float reach=baseMax+ext*40.f;
            for(float f=0.75f; f<=1.0f; f+=0.25f){
                float tx=p.x+ux*reach*f, ty=p.y+uy*reach*f;
                if(tx<10||tx>st->mapWidth-10||ty<10||ty>st->mapHeight-10) continue;
                for(int str=1;str<=3;str++){
                    float s=evaluate(p,tx,ty,ext,str);
                    if(s>bestS){ bestS=s; bestP=i; bestX=tx; bestY=ty; bestExt=ext; bestStr=str; }
                }
            }
        }
    }

    if(bestP<0){ out->valid=0; return 1; }             // 无处可走
    out->valid=1; out->action=AI_ACT_BRANCH;
    out->parentId=bestP; out->targetX=bestX; out->targetY=bestY;
    out->strength=bestStr; out->extend=bestExt;
    if(myScore(st)>=3 && dist2(bestX,bestY,en.x,en.y)<150.f) out->buyExtra=1;  // 贴脸冲刺
    return 1;
}

/* 可选：候选落点热力图（调试用）。
   游戏在每回合决策前调用，把这些点按 score 归一化后以 蓝→黄→红 显示。 */
AI_PLUGIN_EXPORT int aiPluginGetCands(const AIPluginState* st, AIPluginCand* out, int maxCands){
    if(!st || !out || st->apiVersion!=AI_PLUGIN_API_VERSION) return 0;
    int enRoot=-1;
    for(int i=0;i<st->nodeCount;i++)
        if(st->nodes[i].parentId<0 && st->nodes[i].team!=st->myTeam){ enRoot=i; break; }
    if(enRoot<0) return 0;
    const AIPluginNode& en=st->nodes[enRoot];

    int n=0;
    const float base = st->maxBranchLength - 3.f*40.f;   // 基础 120px
    for(int i=0;i<st->nodeCount && n<maxCands;i++){
        const AIPluginNode& p=st->nodes[i];
        if(p.team!=st->myTeam || p.isolated || p.childCount>=2) continue;
        float dx=en.x-p.x, dy=en.y-p.y;
        float pd=std::sqrt(sq(dx)+sq(dy)); if(pd<0.001f) continue;
        float ux=dx/pd, uy=dy/pd;
        for(int ext=0; ext<=3 && n<maxCands; ext++){
            float reach=base+ext*40.f;
            for(float f=0.6f; f<=1.0f; f+=0.2f){
                float tx=p.x+ux*reach*f, ty=p.y+uy*reach*f;
                if(tx<10||tx>st->mapWidth-10||ty<10||ty>st->mapHeight-10) continue;
                // 打分: 越接近敌根越高, 顺路得分点加分, 靠近敌方扣分
                float s = 100.f - dist2(tx,ty,en.x,en.y)*0.4f;
                for(int k=0;k<st->scorePointCount;k++){
                    const AIPluginScorePoint& sp=st->scorePoints[k];
                    if(!sp.alive) continue;
                    float d=dist2(tx,ty,sp.x,sp.y);
                    if(d<80.f) s += sp.value*30.f*(1.f-d/80.f);
                }
                bool bad=false;
                for(int j=0;j<st->nodeCount;j++){
                    const AIPluginNode& q=st->nodes[j];
                    if(q.team==st->myTeam) continue;
                    float d=dist2(tx,ty,q.x,q.y);
                    if(d<st->occupyRadius+2.f){ bad=true; break; }
                    if(d<90.f) s -= (90.f-d)*0.5f;
                }
                if(bad) continue;
                out[n].x=tx; out[n].y=ty; out[n].score=s;
                n++;
            }
        }
    }
    return n;
}

} // extern "C"
