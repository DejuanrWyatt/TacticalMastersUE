"""
Paints big battle maps (64 x 64 tiles) as their top halves, the way map files
hold them: the bottom half is the top turned about 180 degrees by the game.

Tiles: '1'-'9' height levels, '#' rock, '~' water, 'x' embers, '+' spring.
A unit climbs at most 2 levels in one step, so a plateau 3 or more above its
neighbours needs ramps: steps of 1-2 levels.
"""

import json
import math
import os
import random

W, H = 64, 32  # the top half: 64 wide, 32 rows (the whole map is 64 x 64)


class Canvas:
    def __init__(self, base='1', seed=1):
        self.g = [[base] * W for _ in range(H)]
        self.rng = random.Random(seed)

    def inside(self, x, y):
        return 0 <= x < W and 0 <= y < H

    def put(self, x, y, c):
        if self.inside(x, y):
            self.g[y][x] = c

    def get(self, x, y):
        return self.g[y][x] if self.inside(x, y) else None

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, c)

    def ellipse(self, cx, cy, rx, ry, c, wobble=0.0):
        for y in range(int(cy - ry - 2), int(cy + ry + 3)):
            for x in range(int(cx - rx - 2), int(cx + rx + 3)):
                a = math.atan2(y - cy, x - cx)
                r = 1.0 + wobble * (math.sin(3 * a + cx) * 0.5 + math.sin(5 * a + cy) * 0.5)
                if ((x - cx) / (rx * r)) ** 2 + ((y - cy) / (ry * r)) ** 2 <= 1.0:
                    self.put(x, y, c)

    def hill(self, cx, cy, rx, ry, top, wobble=0.15):
        """A stepped hill: rings one level apart up to top, so it can be climbed anywhere."""
        levels = top - 1
        for i in range(levels):
            f = 1.0 - i / levels
            self.ellipse(cx, cy, rx * f, ry * f, str(2 + i), wobble)

    def mesa(self, cx, cy, rx, ry, top, ramps, wobble=0.1):
        """A flat-topped height with cliffs, climbable only up ramps (list of (x, y) tiles at its edge)."""
        self.ellipse(cx, cy, rx, ry, str(top), wobble)
        for (x, y) in ramps:
            self.stairs(x, y, cx, cy, top)

    def stairs(self, x, y, towardx, towardy, top):
        """A ramp two tiles wide from level 1 at (x, y) climbing toward a point, 2 levels a step."""
        dx, dy = towardx - x, towardy - y
        n = max(abs(dx), abs(dy)) or 1
        sx, sy = dx / n, dy / n
        level = 1
        px, py = float(x), float(y)
        while level < top:
            level = min(top, level + 1)
            for ox in (0, 1):
                for oy in (0, 1):
                    tx, ty = int(round(px)) + ox, int(round(py)) + oy
                    if self.inside(tx, ty) and self.get(tx, ty) not in ('~',):
                        cur = self.get(tx, ty)
                        if not cur.isdigit() or int(cur) < level:
                            self.put(tx, ty, str(level))
            px += sx
            py += sy

    def river(self, points, width, c='~'):
        """Water along a line through points, width tiles across."""
        for (ax, ay), (bx, by) in zip(points, points[1:]):
            steps = int(max(abs(bx - ax), abs(by - ay)) * 2) + 1
            for s in range(steps + 1):
                t = s / steps
                x, y = ax + (bx - ax) * t, ay + (by - ay) * t
                self.ellipse(x, y, width / 2, width / 2, c)

    def grove(self, cx, cy, r, count, c='#'):
        """Rocks scattered in a circle: cover to fight through."""
        for _ in range(count):
            a = self.rng.uniform(0, 2 * math.pi)
            d = r * math.sqrt(self.rng.random())
            x, y = int(round(cx + d * math.cos(a))), int(round(cy + d * math.sin(a)))
            if self.get(x, y) and self.get(x, y).isdigit():
                self.put(x, y, c)

    def clear(self, cx, cy, r, level=None):
        """An open clearing: rock and water gone (a camp's room to fight)."""
        for y in range(int(cy - r), int(cy + r) + 1):
            for x in range(int(cx - r), int(cx + r) + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r and self.inside(x, y):
                    if level is not None:
                        self.put(x, y, str(level))
                    elif not self.get(x, y).isdigit():
                        self.put(x, y, '1')

    def rows(self):
        return [''.join(r) for r in self.g]


def whole(rows):
    """The whole map as the game builds it: the top, then the top turned about."""
    return rows + [r[::-1] for r in rows[::-1]]


def save(path, map_id, name, desc, theme, rows, spawns):
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    data = {"format": "tactical-masters-map", "version": 1, "id": map_id, "name": name, "desc": desc,
            "theme": theme, "top": rows, "spawns": spawns}
    with open(path, 'w') as f:
        json.dump(data, f, indent=2)
        f.write('\n')


def analyser_file(path, name, rows, spawns):
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'w') as f:
        f.write(f"// {name}\nname {name}\n")
        for (x, y) in spawns:
            f.write(f"spawn {x} {y}\n")
        for r in rows:
            f.write(r + '\n')


def wall(c, points, gaps=(), c_='#', thick=1):
    """Rock along a line, broken where the gaps (fractions 0-1 of its length, each (start, end)) say."""
    total = sum(math.dist(a, b) for a, b in zip(points, points[1:]))
    walked = 0.0
    for (ax, ay), (bx, by) in zip(points, points[1:]):
        seg = math.dist((ax, ay), (bx, by))
        steps = int(seg * 2) + 1
        for s in range(steps + 1):
            t = s / steps
            f = (walked + seg * t) / total
            if any(g0 <= f <= g1 for g0, g1 in gaps):
                continue
            x, y = ax + (bx - ax) * t, ay + (by - ay) * t
            for ox in range(thick):
                for oy in range(thick):
                    tx, ty = int(round(x)) + ox, int(round(y)) + oy
                    if c.get(tx, ty) and c.get(tx, ty).isdigit():
                        c.put(tx, ty, c_)
        walked += seg
