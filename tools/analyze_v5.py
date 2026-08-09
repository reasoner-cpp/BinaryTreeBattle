# -*- coding: utf-8 -*-
"""V4.4 新5局深度分析: 行为模式 + 黄点争夺 + 胜负转折点"""
import glob, math, sys
sys.path.insert(0, '.')
from analyze_btbdt import parse

RED_R, BLUE_R = (80.0, 80.0), (920.0, 620.0)
def dist(ax, ay, bx, by): return math.hypot(ax-bx, ay-by)

for p in sorted(glob.glob('*.btb') + glob.glob('*.btbdt')):
    h, acts = parse(p)
    sps = h.get('_scores', [])
    print(f"\n========== {p}  winner={h.get('winner')}  ==========")
    # 黄点布局
    if sps:
        print(f"  黄点({len(sps)}): " + "  ".join(f"({s[0]:.0f},{s[1]:.0f})v{s[2]}" for s in sps))
    # 黄点收集精确归属: 落点<18 匹配 [S]
    got = {}   # sp_idx -> team
    for a in acts:
        if a['tm'] is None or a['ty'] != 0: continue
        if a['a'] <= a['b']: continue
        for i, s in enumerate(sps):
            if i in got: continue
            if dist(a['tx'], a['ty2'], s[0], s[1]) < 18:
                got[i] = a['tm']; break
    if sps:
        redV = sum(sps[i][2] for i, t in got.items() if t == 0)
        blueV = sum(sps[i][2] for i, t in got.items() if t == 1)
        ungot = [i for i in range(len(sps)) if i not in got]
        print(f"  收集: 红{sum(1 for t in got.values() if t==0)}颗({redV}分) 蓝{sum(1 for t in got.values() if t==1)}颗({blueV}分) 未收{len(ungot)}颗")
    # 行为统计
    for tm, name, enR in [(0, '红AI', BLUE_R), (1, '蓝人', RED_R)]:
        moves = [a for a in acts if a['tm'] == tm and a['ty'] == 0]
        if not moves: continue
        dmin = min(dist(a['tx'], a['ty2'], enR[0], enR[1]) for a in moves)
        costs = [a['e'] + (a['s'] - 1) for a in moves]
        big = [c for c in costs if c > 2]
        hit = [a for a in moves if a['nk'] + a['ek'] > 0]
        kills = sum(a['nk'] for a in moves)
        # 推进轨迹(每3步)
        traj = [int(dist(a['tx'], a['ty2'], enR[0], enR[1])) for a in moves]
        print(f"  {name}: {len(moves)}步 最近距敌根={dmin:.0f} 均花费={sum(costs)/len(costs):.1f} "
              f">2分大手笔{len(big)}次({big[:6]}) 总杀{kills} 轨迹={traj[::max(1,len(traj)//8)]}")
        # 收集点价值
        col = [a for a in acts if a['tm'] == tm and a['a'] > a['b']]
        print(f"     收集{len(col)}次 强化{sum(1 for a in acts if a['tm']==tm and a['ty']==1)}次 "
              f"额外{sum(1 for a in acts if a['tm']==tm and a['ty']==2)}次")
    # 胜负转折: 最后3个动作
    print(f"  终局3步: " + " | ".join(
        f"t{acts[-3+i]['t']}{'红' if acts[-3+i]['tm']==0 else '蓝'}"
        f"({acts[-3+i]['tx']:.0f},{acts[-3+i]['ty2']:.0f})杀{acts[-3+i]['nk']}"
        for i in range(min(3, len(acts)))))
