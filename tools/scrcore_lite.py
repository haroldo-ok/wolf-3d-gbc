import numpy as np

# ------------------------------------------------------------------ scaling
def _line_costs(p):
    """cost of removing each column of p (h x w): pixels that differ from the less different neighbour"""
    h, w = p.shape
    costs = np.zeros(w)
    for c in range(w):
        cs = []
        if c > 0:
            cs.append(np.count_nonzero(p[:, c] != p[:, c - 1]))
        if c < w - 1:
            cs.append(np.count_nonzero(p[:, c] != p[:, c + 1]))
        costs[c] = min(cs)
    return costs


def _choose(costs, k, gmin, gmax):
    """k indices to drop: consecutive gaps in [gmin, gmax], first < gmax, last >= n - gmax; minimal total cost"""
    n = len(costs)
    INF = 1e18
    dp = np.full((k, n), INF)
    back = np.zeros((k, n), np.int32)
    ideal = n / k
    for i in range(min(gmax, n)):
        dp[0][i] = costs[i] * 16 + abs(i - ideal / 2) * 0.01
    for j in range(1, k):
        prev = dp[j - 1]
        for i in range(n):
            lo, hi = i - gmax, i - gmin
            if hi < 0:
                continue
            lo = max(lo, 0)
            seg = prev[lo:hi + 1]
            a = int(np.argmin(seg))
            if seg[a] >= INF:
                continue
            dp[j][i] = seg[a] + costs[i] * 16 + abs(i - (j + 0.5) * ideal) * 0.01
            back[j][i] = lo + a
    last = dp[k - 1][max(0, n - gmax):]
    i = max(0, n - gmax) + int(np.argmin(last))
    out = [i]
    for j in range(k - 1, 0, -1):
        i = back[j][i]
        out.append(i)
    return sorted(out)


def _bands(p):
    """row bands separated by rows of one colour"""
    h = p.shape[0]
    uni = [len(np.unique(p[r])) == 1 for r in range(h)]
    bands, s = [], None
    for r in range(h + 1):
        if r < h and not uni[r]:
            if s is None:
                s = r
        elif s is not None:
            bands.append((s, r))
            s = None
    return bands, uni


def scale_screen(pix, out_w=256, out_h=192):
    """index image (h x w) -> out_h x out_w; also returns the kept source rows and, per output row, the kept columns"""
    p = np.array(pix, dtype=np.uint8)
    h, w = p.shape
    k = h - out_h
    keep_rows = list(range(h))
    if k:
        drop = set(_choose(_line_costs(p.T), k, h // k - 8, h // k + 8))
        keep_rows = [r for r in range(h) if r not in drop]
        p = p[keep_rows, :]
    h = p.shape[0]
    k = w - out_w
    out = np.zeros((h, out_w), np.uint8)
    bands, uni = _bands(p)
    for r in range(h):
        if uni[r]:
            out[r, :] = p[r, 0]
    colmap = np.zeros((h, out_w), np.int32)
    for r in range(h):
        colmap[r] = np.arange(out_w) * w // out_w
    for (a, b) in bands:
        seg = p[a:b]
        drop = set(_choose(_line_costs(seg), k, 3, 7))
        keep = [c for c in range(w) if c not in drop]
        out[a:b] = seg[:, keep]
        colmap[a:b] = keep
    return out, keep_rows, colmap


