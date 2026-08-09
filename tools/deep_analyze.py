# -*- coding: utf-8 -*-
"""深度分析: 胜方(人类)战术特征 — 推进/花费纪律/收集节奏/防守"""
import glob, math, sys
sys.path.insert(0, '.')
from analyze_btbdt import parse

RED_R, BLUE_R = (80.0, 80.0), (920.0, 620.0)

def dist(ax, ay, bx, by): return math.hypot(ax-bx, ay-by)

for p in sorted(glob.glob('*.btbdt')):
    h, acts = parse(p)
    print(f"\n========== {p} winner={h.get('winner')} ==========")
    for tm, name, enR in [(0, '红AI', BLUE_R), (1, '蓝人', RED_R)]:
        moves = [a for a in acts if a['tm'] == tm and a['ty'] == 0]
        if not moves: continue
        dmin = min(dist(a['tx'], a['ty2'], enR[0], enR[1]) for a in moves)
        # 推进轨迹: 每隔3步的距敌根距离
        traj = [int(dist(a['tx'], a['ty2'], enR[0], enR[1])) for a in moves]
        costs = [a['e'] + (a['s'] - 1) for a in moves]
        big = [c for c in costs if c > 3]
        hit_moves = [a for a in moves if a['nk'] + a['ek'] > 0]
        cost_of_hits = [a['e'] + (a['s'] - 1) for a in hit_moves]
        # 强化时机
        reinf = [a for a in acts if a['tm'] == tm and a['ty'] == 1]
        extra = [a for a in acts if a['tm'] == tm and a['ty'] == 9]
        print(f"  {name}: {len(moves)}步 最近距敌根={dmin:.0f}  轨迹={traj[::3]}")
        print(f"    平均花费={sum(costs)/len(costs):.1f}  >3分的大手笔={len(big)}次({big})  "
              f"强化{len(reinf)}次 额外行动{len(extra)}次")
        if hit_moves:
            hc = len(hit_moves)
            print(f"    攻击步{hc}: 平均花费={sum(cost_of_hits)/len(cost_of_hits):.1f}  "
                  f"平均命中={sum(a['nk']+a['ek'] for a in hit_moves)/len(hit_moves):.1f}")
        # 收集节奏
        col = [a for a in acts if a['tm'] == tm and a['a'] > a['b']]
        if col: print(f"    收集{len(col)}次, 收集发生在步号{[acts.index(c) for c in col][:8]}")
