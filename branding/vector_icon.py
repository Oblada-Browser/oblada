#!/usr/bin/env python3
"""Writes the one-color logo (oblada-glyph.svg) as a Chromium vector icon (.icon), for the
places Chromium draws its product icon in one color: the address bar's chip on internal
pages, menus, notifications.

usage: vector_icon.py <out.icon>
       vector_icon.py --svg <size> [<viewBox min-y>]   (one SVG path, for WebUI iconsets)

The glyph is a rounded square with the tail cut out of it and the spot left in: the .icon
draws the square, clears the tail's path, then draws the spot, on Chromium's 24-unit
canvas with the square at 2..22 like its own product icons.
"""
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

HERE = pathlib.Path(__file__).resolve().parent
SVG = '{http://www.w3.org/2000/svg}'
CANVAS, MARGIN = 24, 2


def main(out):
    root = ET.parse(HERE / 'oblada-glyph.svg').getroot()
    rect = root.find(f'{SVG}mask/{SVG}rect')
    tail = root.find(f'{SVG}mask/{SVG}path')
    spot = root.find(f'{SVG}mask/{SVG}circle')
    x0, y0, size = (float(rect.get(k)) for k in ('x', 'y', 'width'))
    scale = (CANVAS - 2 * MARGIN) / size

    def num(v):
        text = f'{v:.2f}'.rstrip('0').rstrip('.')
        return text + 'f' if '.' in text else text

    def point(x, y):
        return f'{num(MARGIN + (x - x0) * scale)}, {num(MARGIN + (y - y0) * scale)}'

    lines = [
        '// Oblada\'s logo in one color. Made from branding/oblada-glyph.svg by',
        '// branding/vector_icon.py: change those, not this.',
        '',
        f'CANVAS_DIMENSIONS, {CANVAS},',
        f'ROUND_RECT, {MARGIN}, {MARGIN}, {CANVAS - 2 * MARGIN}, {CANVAS - 2 * MARGIN}, '
        f'{num(float(rect.get("rx")) * scale)},',
        'NEW_PATH,',
        'PATH_MODE_CLEAR,',
    ]
    tokens = re.findall(r'[MLCZ]|-?\d+(?:\.\d+)?', tail.get('d'))
    command, args = None, []
    for token in tokens:
        if token in 'MLCZ':
            command = token
            if command == 'Z':
                lines.append('CLOSE,')
            continue
        args.append(float(token))
        arity = 6 if command == 'C' else 2
        if len(args) < arity:
            continue
        pairs = ', '.join(point(args[i], args[i + 1]) for i in range(0, arity, 2))
        name = {'M': 'MOVE_TO', 'L': 'LINE_TO', 'C': 'CUBIC_TO'}[command]
        lines.append(f'{name}, {pairs},')
        if command == 'M':
            command = 'L'  # further pairs after a move are lines
        args = []
    lines += [
        'NEW_PATH,',
        f'CIRCLE, {point(float(spot.get("cx")), float(spot.get("cy")))}, '
        f'{num(float(spot.get("r")) * scale)}',
    ]
    pathlib.Path(out).write_text('\n'.join(lines) + '\n')


def bezier(p, t):
    """The point at t on the cubic p (four points), and p split there."""
    def lerp(a, b):
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
    ab, bc, cd = lerp(p[0], p[1]), lerp(p[1], p[2]), lerp(p[2], p[3])
    abc, bcd = lerp(ab, bc), lerp(bc, cd)
    m = lerp(abc, bcd)
    return m, (p[0], ab, abc, m), (m, bcd, cd, p[3])


def svg_outline(size, min_y=0):
    """The glyph as one SVG path for a `size`-wide icon (its viewBox starting at 0, min_y): the
    rounded square without the tail, which enters it on the left and leaves at the bottom, and
    the spot, inside the tail."""
    root = ET.parse(HERE / 'oblada-glyph.svg').getroot()
    rect = root.find(f'{SVG}mask/{SVG}rect')
    spot = root.find(f'{SVG}mask/{SVG}circle')
    x0, y0, side, rx = (float(rect.get(k)) for k in ('x', 'y', 'width', 'rx'))
    x1, y1 = x0 + side, y0 + side
    tokens = re.findall(r'[MLCZ]|-?\d+(?:\.\d+)?', root.find(f'{SVG}mask/{SVG}path').get('d'))
    # The tail's outline as cubics (lines as degenerate ones), in order.
    segments, here, command, args = [], None, None, []
    for token in tokens:
        if token in 'MLCZ':
            command = token
            continue
        args.append(float(token))
        arity = 6 if command == 'C' else 2
        if len(args) < arity:
            continue
        points = [(args[i], args[i + 1]) for i in range(0, arity, 2)]
        if command == 'M':
            here, command = points[0], 'L'
        elif command == 'L':
            segments.append((here, here, points[0], points[0]))
            here = points[0]
        else:
            segments.append((here, *points))
            here = points[2]
        args = []

    def inside(p):
        return x0 < p[0] < x1 and y0 < p[1] < y1

    def crossing(segment, entering):
        lo, hi = 0.0, 1.0
        for _ in range(60):
            mid = (lo + hi) / 2
            if inside(bezier(segment, mid)[0]) == entering:
                hi = mid
            else:
                lo = mid
        return hi if entering else lo

    first = next(i for i, seg in enumerate(segments) if not inside(seg[0]) and inside(seg[3]))
    last = next(i for i, seg in enumerate(segments) if inside(seg[0]) and not inside(seg[3]))
    curves = list(segments[first:last + 1])
    curves[0] = bezier(curves[0], crossing(curves[0], True))[2]
    curves[-1] = bezier(curves[-1], crossing(curves[-1], False))[1]
    enter, leave = curves[0][0], curves[-1][3]
    # On the straight parts of the left and bottom edges, as the square's path below assumes.
    assert abs(enter[0] - x0) < 1e-6 and y0 + rx < enter[1] < y1 - rx, enter
    assert abs(leave[1] - y1) < 1e-6 and x0 + rx < leave[0] < x1 - rx, leave

    scale = size * 10 / 12 / side
    margin = size / 12

    def n(v):
        return f'{v:.3f}'.rstrip('0').rstrip('.')

    def pt(p):
        return f'{n(margin + (p[0] - x0) * scale)} {n(min_y + margin + (p[1] - y0) * scale)}'

    r = n(rx * scale)
    d = [f'M{pt(enter)}']
    d += [f'C{pt(c[1])} {pt(c[2])} {pt(c[3])}' for c in curves]
    # Back around the square the long way: right along the bottom, up, left, down.
    for corner_start, corner_end, edge_end in [
            ((x1 - rx, y1), (x1, y1 - rx), (x1, y0 + rx)),
            ((x1, y0 + rx), (x1 - rx, y0), (x0 + rx, y0)),
            ((x0 + rx, y0), (x0, y0 + rx), enter)]:
        d.append(f'L{pt(corner_start)}A{r} {r} 0 0 0 {pt(corner_end)}')
        if edge_end is not enter:
            d.append(f'L{pt(edge_end)}')
    d.append('Z')
    # The spot, turning the same way.
    cx, cy, radius = (float(spot.get(k)) for k in ('cx', 'cy', 'r'))
    sr = n(radius * scale)
    d.append(f'M{pt((cx - radius, cy))}A{sr} {sr} 0 1 0 {pt((cx + radius, cy))}'
             f'A{sr} {sr} 0 1 0 {pt((cx - radius, cy))}Z')
    return ''.join(d)


if __name__ == '__main__':
    if sys.argv[1] == '--svg':
        print(svg_outline(float(sys.argv[2]), float(sys.argv[3]) if len(sys.argv) > 3 else 0))
    else:
        main(sys.argv[1])
