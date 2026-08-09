# -*- coding: utf-8 -*-
"""分析 btbdt 人机对战数据: 行为模式 / AI 弱点"""
import glob, os, sys, math

def parse(path):
    acts = []
    cur = None
    header = {}
    scores = []   # [S] 段: (x, y, v)
    in_sp = False
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.strip()
        if line.startswith('['):
            tag = line.strip('[]')
            if tag == 'S':
                in_sp = True; sp = {}; continue
            if tag == 'A':
                in_sp = False
                if cur: acts.append(cur)
                cur = {'tm': -1, 'ty': -1, 'px': 0, 'py': 0, 'tx': 0, 'ty2': 0,
                       's': 1, 'e': 0, 'b': 0, 'a': 0, 'nk': 0, 'ek': 0}
                continue
        if '=' in line:
            k, v = line.split('=', 1)
            if in_sp:
                try: sp[k] = float(v) if k in ('x','y') else int(float(v))
                except: pass
                if k == 'v': scores.append((sp.get('x',0), sp.get('y',0), sp.get('v',1)))
                continue
            if cur is not None:
                try:
                    cur[k] = float(v) if k in ('px','py','tx','ty2') else int(float(v))
                except: pass
            else:
                header[k] = v
    if cur: acts.append(cur)
    header['_scores'] = scores
    return header, acts

def dist(ax, ay, bx, by): return math.hypot(ax-bx, ay-by)

def analyze(path):
    h, acts = parse(path)
    winner = h.get('winner', '?')
    # 红方根 (80,80), 蓝方根 (920,620)
    RED_R, BLUE_R = (80.0, 80.0), (920.0, 620.0)
    stats = {0: {'n':0,'atk':0,'def':0,'col':0,'ext':0,'str':0,'dist2en':0.0,'hit':0,'kills':0,'toRoot':0},
             1: {'n':0,'atk':0,'def':0,'col':0,'ext':0,'str':0,'dist2en':0.0,'hit':0,'kills':0,'toRoot':0}}
    seq = []
    for a in acts:
        tm = a['tm']
        st = stats[tm]
        st['n'] += 1
        typ = a['ty']
        # 收集判定
        collected = a['a'] > a['b']
        if typ == 0:
            if a['nk'] + a['ek'] > 0: st['atk'] += 1
            else: st['def'] += 1
            if collected: st['col'] += 1
        elif typ == 1: st['def'] += 1
        st['ext'] += a['e']
        st['str'] += a['s']
        st['hit'] += a['nk'] + a['ek']
        st['kills'] += a['nk']
        # 目标离敌方根距离
        enR = BLUE_R if tm == 0 else RED_R
        st['dist2en'] += dist(a['tx'], a['ty2'], enR[0], enR[1])
        if typ == 0:
            seq.append((tm, a['tx'], a['ty2'], a['nk'], a['ek'], a['e'], a['s'], a['b'], a['a']))
    out = {}
    for tm in (0, 1):
        s = stats[tm]
        n = max(1, s['n'])
        out[f'P{tm}'] = {
            'moves': s['n'], 'atk': s['atk'], 'def': s['def'], 'col': s['col'],
            'avg_ext': s['ext']/n, 'avg_str': s['str']/n,
            'avg_dist2enRoot': s['dist2en']/n,
            'total_hits': s['hit'], 'total_nodeKills': s['kills'],
        }
    return winner, h, out, seq

print(f"{'文件':<38} {'胜者':<6} {'回合':<5} {'红走/攻/守/收':<18} {'蓝走/攻/守/收':<18} {'红均距敌根':<10} {'蓝均距敌根':<10}")
for p in sorted(glob.glob('*.btbdt')):
    w, h, o, seq = analyze(p)
    r, b = o['P0'], o['P1']
    print(f"{p:<38} {w:<6} {len(seq):<5} "
          f"{r['moves']}/{r['atk']}/{r['def']}/{r['col']:<12} "
          f"{b['moves']}/{b['atk']}/{b['def']}/{b['col']:<12} "
          f"{r['avg_dist2enRoot']:<10.0f} {b['avg_dist2enRoot']:<10.0f}")
