# -*- coding: utf-8 -*-
"""打印单局完整时间线"""
import sys, math
sys.path.insert(0, '.')
from analyze_btbdt import parse

def dist(ax, ay, bx, by): return math.hypot(ax-bx, ay-by)

RED_R, BLUE_R = (80.0, 80.0), (920.0, 620.0)

def dump(path, maxn=100000):
    h, acts = parse(path)
    print(f"===== {path}  winner={h.get('winner')} =====")
    for i, a in enumerate(acts):
        tm = a['tm']
        typ = a['ty']
        tname = ['扩张','强化','?','?','?','?','?','?','?','额外'][typ] if typ < 10 else f't{typ}'
        px, py, tx, ty2 = a['px'], a['py'], a['tx'], a['ty2']
        s, e = a['s'], a['e']
        b, aa = a['b'], a['a']
        nk, ek = a['nk'], a['ek']
        team = '红AI' if tm == 0 else '蓝人'
        if typ == 0:
            dEn = dist(tx, ty2, BLUE_R[0], BLUE_R[1]) if tm == 0 else dist(tx, ty2, RED_R[0], RED_R[1])
            print(f"t{i:>3} {team} 扩张 ({px:>5.0f},{py:>5.0f})->({tx:>5.0f},{ty2:>5.0f}) 强度{s} 扩展{e} 命中{nk}杀{ek} 分{b}->{aa} 距敌根{dEn:>5.0f}")
        else:
            print(f"t{i:>3} {team} {tname} 分{b}->{aa}")
        if i > maxn: break

if __name__ == '__main__':
    dump(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 100000)
