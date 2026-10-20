#!/usr/bin/env python3
"""Draws the Oblada logo: the tail of an occhiata (Oblada melanura, the saddled seabream) with its black spot.

Writes next to this file:
  oblada-icon.svg   full-color app icon on Apple's 1024 grid (824 squircle, 100 margin)
  oblada-tile.svg   the same artwork filling its square, for logos inside the browser
  oblada-glyph.svg  one color (currentColor), full bleed, for monochrome use
  AppIcon.icon/     the icon as an Icon Composer document (background, fish and spot as
                    separate layers), which macOS 26 and later render with Liquid Glass
export.sh turns these into the files Chromium's Mac build takes its branding from.

The tail is drawn in its own frame (origin at the narrowest point of the tail stalk, +x toward
the fin), which is then rotated into place. The body is an arc that bends toward the upper side
as it leaves the frame, so the fish reads as turning, like in the sketch.
"""
import json
import math
import pathlib

OUT = pathlib.Path(__file__).resolve().parent

ORIGIN = (525, 550)          # the tail stalk's narrowest point, in icon pixels
ANGLE = -50                  # the fin's axis, degrees (negative = up and to the right)
WAIST = 76                   # half the stalk's height at its narrowest
# Upper half of the fin; the lower half mirrors it. Cubic control points, then the end point.
LEADING_EDGE = [(50, -125), (150, -220), (230, -235)]   # waist -> lobe tip
TRAILING_EDGE = [(195, -135), (165, -40), (152, 0)]    # lobe tip -> fork
BODY_LENGTH, BODY_BEND, BODY_HALF_WIDTH = 560, 18, 190  # widest at BODY_LENGTH, past the icon's edge
SPOT_AT, SPOT_R = 38, 74     # distance from the waist along the body, radius
RAYS = 14

SEA = ('#2ba3e3', '#0a3c8c')
SILVER = ('#f4f8fc', '#dfe9f3', '#a3c3df')
SPOT = '#0b1426'
RAY = '#5f8fbf'
SHADOW = '#04193d'


def rotate(p):
    c, s = math.cos(math.radians(ANGLE)), math.sin(math.radians(ANGLE))
    return (ORIGIN[0] + p[0]*c - p[1]*s, ORIGIN[1] + p[0]*s + p[1]*c)


def fmt(*points):
    return ' '.join(f'{x:.1f} {y:.1f}' for x, y in map(rotate, points))


def body_point(s, side):
    """A point s along the body; side is -1 for the upper edge, +1 for the lower, 0 for the middle."""
    k = math.radians(BODY_BEND) / BODY_LENGTH
    heading = (-math.cos(k*s), -math.sin(k*s))
    normal = (heading[1], -heading[0])
    center = (-math.sin(k*s)/k, -(1 - math.cos(k*s))/k)
    # eases out of the stalk and into the rounded back of a deep, oval body
    w = WAIST + (BODY_HALF_WIDTH - WAIST) * (1 - math.cos(math.pi * s / BODY_LENGTH)) / 2
    return (center[0] + side*normal[0]*w, center[1] + side*normal[1]*w)


def body_edge(side, segments=6):
    """The body edge from the waist outward, as cubic segments with numeric tangents."""
    def tangent(s):
        a, b = body_point(min(s + .5, BODY_LENGTH), side), body_point(max(s - .5, 0), side)
        m = math.dist(a, b)
        return ((a[0] - b[0])/m, (a[1] - b[1])/m)
    out = []
    for i in range(segments):
        s0, s1 = BODY_LENGTH*i/segments, BODY_LENGTH*(i + 1)/segments
        p0, p1 = body_point(s0, side), body_point(s1, side)
        t0, t1 = tangent(s0), tangent(s1)
        l = math.dist(p0, p1) / 3
        out.append((p0, (p0[0] + t0[0]*l, p0[1] + t0[1]*l), (p1[0] - t1[0]*l, p1[1] - t1[1]*l), p1))
    return out


def mirror(points):
    return [(x, -y) for x, y in points]


def fish_path():
    upper, lower = body_edge(-1), body_edge(+1)
    # upper body edge inward, around the fin, lower body edge outward
    d = [f'M {fmt(extend(upper[-1]))} L {fmt(upper[-1][3])}']
    d += [f'C {fmt(c1, c0, p0)}' for p0, c0, c1, _ in reversed(upper)]
    d.append(f'C {fmt(*LEADING_EDGE)}')
    d.append(f'C {fmt(*TRAILING_EDGE)}')
    lower_fin = mirror([LEADING_EDGE[2], TRAILING_EDGE[0], TRAILING_EDGE[1], TRAILING_EDGE[2]])
    d.append(f'C {fmt(lower_fin[2], lower_fin[1], lower_fin[0])}')
    d.append(f'C {fmt(*mirror([LEADING_EDGE[1], LEADING_EDGE[0], (0, -WAIST)]))}')
    d += [f'C {fmt(c0, c1, p1)}' for _, c0, c1, p1 in lower]
    # carry both edges on past the icon's edge so the closing line never shows
    d.append(f'L {fmt(extend(lower[-1]), extend(upper[-1]))} Z')
    return ' '.join(d)


def extend(segment, by=600):
    _, _, c1, p1 = segment
    m = math.dist(c1, p1)
    return (p1[0] + (p1[0] - c1[0])/m*by, p1[1] + (p1[1] - c1[1])/m*by)


def bezier(p, t):
    u = 1 - t
    return tuple(u**3*p[0][i] + 3*u*u*t*p[1][i] + 3*u*t*t*p[2][i] + t**3*p[3][i] for i in (0, 1))


def rays():
    """Fin rays: from the fin's base to evenly spaced points on its trailing edge, bowed outward."""
    half = RAYS // 2
    upper = [bezier([LEADING_EDGE[2], *TRAILING_EDGE], t/(half + 1)) for t in range(1, half + 1)]
    ends = upper + list(reversed(mirror(upper)))
    out = []
    for i, end in enumerate(ends):
        start = (-10, WAIST*.8*(2*i/(len(ends) - 1) - 1))
        mid = ((start[0] + end[0])/2, (start[1] + end[1])/2*1.12)
        out.append(f'<path d="M {fmt(start)} Q {fmt(mid, end)}"/>')
    return ''.join(out)


def spot():
    return rotate(body_point(SPOT_AT, 0))


SQUIRCLE = 'x="100" y="100" width="824" height="824" rx="185"'
FULL_BLEED = 'viewBox="100 100 824 824" width="1024" height="1024"'


def silver_gradient():
    return f"""<linearGradient id="silver" gradientUnits="userSpaceOnUse" x1="200" y1="800" x2="700" y2="300">
      <stop offset="0" stop-color="{SILVER[0]}"/>
      <stop offset=".5" stop-color="{SILVER[1]}"/>
      <stop offset="1" stop-color="{SILVER[2]}"/>
    </linearGradient>"""


def artwork():
    """The fish on the sea, clipped to the squircle."""
    fish, (sx, sy) = fish_path(), spot()
    return f"""<defs>
    <linearGradient id="sea" x1="0" y1="0" x2=".3" y2="1">
      <stop offset="0" stop-color="{SEA[0]}"/>
      <stop offset="1" stop-color="{SEA[1]}"/>
    </linearGradient>
    {silver_gradient()}
    <filter id="lift" x="-20%" y="-20%" width="140%" height="140%" color-interpolation-filters="sRGB">
      <feGaussianBlur in="SourceAlpha" stdDeviation="16"/>
      <feOffset dy="12" result="blur"/>
      <feFlood flood-color="{SHADOW}" flood-opacity=".35"/>
      <feComposite in2="blur" operator="in"/>
      <feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
    <clipPath id="squircle"><rect {SQUIRCLE}/></clipPath>
    <clipPath id="fish"><path d="{fish}"/></clipPath>
  </defs>
  <rect {SQUIRCLE} fill="url(#sea)"/>
  <g clip-path="url(#squircle)">
    <path fill="url(#silver)" filter="url(#lift)" d="{fish}"/>
    {fin_rays()}
    <circle cx="{sx:.1f}" cy="{sy:.1f}" r="{SPOT_R}" fill="{SPOT}" clip-path="url(#fish)"/>
  </g>"""


def fin_rays():
    return f'<g clip-path="url(#fish)" fill="none" stroke="{RAY}" stroke-opacity=".3" stroke-width="3" stroke-linecap="round">{rays()}</g>'


def icon():
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">
  <filter id="tile-shadow" x="-10%" y="-10%" width="120%" height="125%"><feGaussianBlur stdDeviation="14"/></filter>
  <rect {SQUIRCLE} fill="#000" fill-opacity=".3" transform="translate(0 10)" filter="url(#tile-shadow)"/>
  {artwork()}
</svg>
"""


def tile():
    return f"""<svg xmlns="http://www.w3.org/2000/svg" {FULL_BLEED}>
  {artwork()}
</svg>
"""


def glyph():
    fish, (sx, sy) = fish_path(), spot()
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="100 100 824 824" width="824" height="824">
  <mask id="cut">
    <rect {SQUIRCLE} fill="#fff"/>
    <path fill="#000" d="{fish}"/>
    <circle cx="{sx:.1f}" cy="{sy:.1f}" r="{SPOT_R}" fill="#fff"/>
  </mask>
  <rect {SQUIRCLE} fill="currentColor" mask="url(#cut)"/>
</svg>
"""


def color(hex_color):
    r, g, b = (int(hex_color[i:i + 2], 16) / 255 for i in (1, 3, 5))
    return f'extended-srgb:{r:.5f},{g:.5f},{b:.5f},1.00000'


def app_icon(folder):
    """An Icon Composer document. Its canvas is the squircle itself; macOS masks and lights it."""
    (folder / 'Assets').mkdir(parents=True, exist_ok=True)
    fish, (sx, sy) = fish_path(), spot()
    (folder / 'Assets' / 'fish.svg').write_text(f"""<svg xmlns="http://www.w3.org/2000/svg" {FULL_BLEED}>
  <defs>
    {silver_gradient()}
    <clipPath id="fish"><path d="{fish}"/></clipPath>
  </defs>
  <path fill="url(#silver)" d="{fish}"/>
  {fin_rays()}
</svg>
""")
    (folder / 'Assets' / 'spot.svg').write_text(f"""<svg xmlns="http://www.w3.org/2000/svg" {FULL_BLEED}>
  <circle cx="{sx:.1f}" cy="{sy:.1f}" r="{SPOT_R}" fill="{SPOT}"/>
</svg>
""")
    doc = {
        'fill': {
            'linear-gradient': [color(SEA[0]), color(SEA[1])],
            'orientation': {'start': {'x': 0, 'y': 0}, 'stop': {'x': .3, 'y': 1}},
        },
        'groups': [{
            'layers': [
                {'image-name': 'spot.svg', 'name': 'spot', 'glass': False},
                {'image-name': 'fish.svg', 'name': 'fish', 'glass': True},
            ],
            'shadow': {'kind': 'neutral', 'opacity': .5},
            'specular': True,
            'translucency': {'enabled': False, 'value': .5},
        }],
        'supported-platforms': {'squares': ['macOS']},
    }
    (folder / 'icon.json').write_text(json.dumps(doc, indent=2) + '\n')


if __name__ == '__main__':
    (OUT / 'oblada-icon.svg').write_text(icon())
    (OUT / 'oblada-tile.svg').write_text(tile())
    (OUT / 'oblada-glyph.svg').write_text(glyph())
    app_icon(OUT / 'AppIcon.icon')
