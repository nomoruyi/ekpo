#!/usr/bin/env python3
"""Generates the Ekpo logo drafts: one ivory mask drawing on several red/yellow backgrounds.

The mask follows the 16th-century ivory pendant mask of Queen Mother Idia (Benin
Kingdom): a crown of small heads, a beaded cap, two forehead inlays, almond eyes
and an interlaced collar. Design B (Ankara) was chosen on 2026-11-06; it is also
written to packaging/ as the app icon. Run: python3 design/logo/make_logos.py
"""

import math
from pathlib import Path

OUT = Path(__file__).parent

IVORY = "#F4ECD6"
IVORY_SHADE = "#E4D6B6"
INK = "#1B130B"
INLAY = "#3A2414"
RED = "#B3161C"
RED_DARK = "#8A1015"
YELLOW = "#F6BE1A"


def bezier(p0, p1, p2, p3, t):
    u = 1 - t
    return tuple(u**3 * a + 3 * u * u * t * b + 3 * u * t * t * c + t**3 * d for a, b, c, d in zip(p0, p1, p2, p3))


def crown_heads():
    """Ten small bearded heads on an arc above the cap, each facing outward."""
    cx, cy, rx, ry = 100, 124, 49, 65
    uses = []
    for i in range(10):
        a = math.radians(168 - i * 156 / 9)
        x, y = cx + rx * math.cos(a), cy - ry * math.sin(a)
        nx, ny = math.cos(a) / rx, -math.sin(a) / ry
        rot = math.degrees(math.atan2(nx, -ny))
        uses.append(f'<use href="#ekpo-head" transform="translate({x:.1f} {y:.1f}) rotate({rot:.1f}) scale(1.3)"/>')
    return "\n".join(uses)


def collar_beads():
    """Small loops along the lower edge of the collar."""
    left = ((46, 206), (46, 256), (70, 298), (100, 302))
    right = ((100, 302), (130, 298), (154, 256), (154, 206))
    beads = []
    for curve in (left, right):
        for k in range(1, 8):
            x, y = bezier(*curve, k / 8)
            beads.append(f'<circle cx="{x:.1f}" cy="{y + 3:.1f}" r="3.6"/>')
    return "\n".join(beads)


def mask_defs():
    """Group "ekpo-mask" drawn in a 200x320 box; the face centre line is x=100.
    Plain groups and <use> only: Qt SVG (used for Plasma icons) ignores the size
    given to a <symbol> and clip paths defined inside a reused group."""
    return f"""
<pattern id="ekpo-scales" width="8" height="7" patternUnits="userSpaceOnUse">
  <path d="M0,7 Q4,2 8,7 M-4,3.5 Q0,-1.5 4,3.5 M4,3.5 Q8,-1.5 12,3.5" fill="none" stroke="{INK}" stroke-width="0.8" opacity="0.45"/>
</pattern>
<pattern id="ekpo-plait" width="14" height="11" patternUnits="userSpaceOnUse">
  <path d="M0,5.5 C3.5,0.5 10.5,0.5 14,5.5 C10.5,10.5 3.5,10.5 0,5.5 Z" fill="none" stroke="{INK}" stroke-width="1.1" opacity="0.8"/>
  <circle cx="7" cy="5.5" r="1.2" fill="{INK}" opacity="0.8"/>
</pattern>
<pattern id="ekpo-ticks" width="5" height="10" patternUnits="userSpaceOnUse">
  <path d="M2.5,1 V9" stroke="{INK}" stroke-width="1"/>
</pattern>
<g id="ekpo-head" stroke="{INK}" stroke-linejoin="round">
  <path d="M-4,1 V-4 H4 V1 Z" fill="{IVORY}" stroke-width="1"/>
  <ellipse cx="0" cy="-10" rx="6" ry="7" fill="{IVORY}" stroke-width="1.2"/>
  <path d="M-6.5,-14 Q-6,-21 0,-21.5 Q6,-21 6.5,-14 Z" fill="{IVORY}" stroke-width="1.1"/>
  <path d="M-8.5,-14 H8.5" stroke-width="1.6" stroke-linecap="round"/>
  <circle cx="-2.3" cy="-10.5" r="0.95" fill="{INK}" stroke="none"/>
  <circle cx="2.3" cy="-10.5" r="0.95" fill="{INK}" stroke="none"/>
  <path d="M-4,-6.5 Q0,-1.2 4,-6.5" fill="none" stroke-width="0.9"/>
  <path d="M-1.6,-5 V-3.3 M0,-4.6 V-2.8 M1.6,-5 V-3.3" stroke-width="0.7"/>
</g>
<clipPath id="ekpo-eye-clip"><path d="M63.5,173 Q77,162.5 91.5,172.3 Q77,182 63.5,173 Z"/></clipPath>
<g id="ekpo-eye">
  <path d="M59,160 Q76,150 94,158" fill="none" stroke="{INK}" stroke-width="1.2" stroke-linecap="round"/>
  <path d="M60,173 Q77,158 95,172 Q77,186 60,173 Z" fill="{INK}"/>
  <path d="M63.5,173 Q77,162.5 91.5,172.3 Q77,182 63.5,173 Z" fill="{IVORY_SHADE}"/>
  <g clip-path="url(#ekpo-eye-clip)">
    <circle cx="78.5" cy="173.5" r="5.8" fill="{INLAY}" stroke="{INK}" stroke-width="1.2"/>
    <circle cx="80" cy="171.6" r="1.2" fill="{IVORY}"/>
  </g>
  <path d="M57.5,171.5 Q77,154.5 97,170" fill="none" stroke="{INK}" stroke-width="2.6" stroke-linecap="round"/>
</g>
<g id="ekpo-mask">
  <g stroke="{INK}" stroke-linejoin="round" stroke-linecap="round">
    {crown_heads()}
    <path d="M50,126 A50,66 0 0 1 150,126 Z" fill="{IVORY}" stroke-width="2"/>
    <path d="M50,126 A50,66 0 0 1 150,126 Z" fill="url(#ekpo-scales)" stroke="none"/>
    <rect x="32" y="127" width="22" height="13" rx="3.5" fill="{IVORY_SHADE}" stroke-width="1.8"/>
    <rect x="146" y="127" width="22" height="13" rx="3.5" fill="{IVORY_SHADE}" stroke-width="1.8"/>
    <path d="M37,130 V137 M42,130 V137 M47,130 V137 M153,130 V137 M158,130 V137 M163,130 V137" stroke-width="1"/>
    <path d="M48,124 H152 L150.5,135 H49.5 Z" fill="{IVORY_SHADE}" stroke-width="1.8"/>
    <path d="M48,124 H152 L150.5,135 H49.5 Z" fill="url(#ekpo-ticks)" stroke="none"/>
    <path d="M52,135 C51,161 52,187 57,209 C64,243 82,269 100,272 C118,269 136,243 143,209 C148,187 149,161 148,135 Z" fill="{IVORY}" stroke-width="2.2"/>
    <path d="M52.5,165 C43,164 40,191 53.5,197 M51.5,173 C47,175 47,187 52.5,190" fill="{IVORY}" stroke-width="1.6"/>
    <path d="M147.5,165 C157,164 160,191 146.5,197 M148.5,173 C153,175 153,187 147.5,190" fill="{IVORY}" stroke-width="1.6"/>
    <path d="M88.5,139 H93.5 L92.9,168 H89.1 Z M106.5,139 H111.5 L110.9,168 H107.1 Z" fill="{INLAY}" stroke-width="1.2"/>
    <path d="M72,145 V155 M128,145 V155 M77,143 V152 M123,143 V152" stroke-width="0.8" opacity="0.5"/>
  </g>
  <use href="#ekpo-eye" transform="translate(2 8)"/>
  <use href="#ekpo-eye" transform="translate(198 8) scale(-1 1)"/>
  <g stroke="{INK}" stroke-linejoin="round" stroke-linecap="round" transform="translate(0 8)">
    <path d="M95.5,171 C94.5,186 92.5,197 90.5,205 M104.5,171 C105.5,186 107.5,197 109.5,205" fill="none" stroke-width="1.3"/>
    <path d="M83.5,211 C83.5,203.5 91.5,202.5 94,207.5 C96,212 104,212 106,207.5 C108.5,202.5 116.5,203.5 116.5,211 C116.5,218 108.5,220 104,216.5 C102,218.5 98,218.5 96,216.5 C91.5,220 83.5,218 83.5,211 Z" fill="{IVORY}" stroke-width="1.7"/>
    <ellipse cx="91" cy="212.5" rx="3.2" ry="2.1" fill="{INK}" stroke="none"/>
    <ellipse cx="109" cy="212.5" rx="3.2" ry="2.1" fill="{INK}" stroke="none"/>
    <path d="M80,231 C88,223.5 95,225.5 100,228 C105,225.5 112,223.5 120,231 C112,233.5 88,233.5 80,231 Z" fill="{IVORY}" stroke-width="1.6"/>
    <path d="M81,232 C90,234.5 110,234.5 119,232 C113,245 87,245 81,232 Z" fill="{IVORY}" stroke-width="1.6"/>
    <path d="M80.5,231.5 C90,234.5 110,234.5 119.5,231.5" fill="none" stroke-width="2.2"/>
    <path d="M93,253 Q100,256 107,253" fill="none" stroke-width="0.9"/>
    <rect x="37" y="199" width="21" height="12" rx="3.5" fill="{IVORY_SHADE}" stroke-width="1.8"/>
    <rect x="142" y="199" width="21" height="12" rx="3.5" fill="{IVORY_SHADE}" stroke-width="1.8"/>
    <g fill="{IVORY}" stroke-width="1.4">{collar_beads()}</g>
    <path id="ekpo-collar" d="M46,206 C46,256 70,298 100,302 C130,298 154,256 154,206 H138.5 C138.5,240 121,264 100,266.5 C79,264 61.5,240 61.5,206 Z" fill="{IVORY_SHADE}" stroke-width="2"/>
    <path d="M46,206 C46,256 70,298 100,302 C130,298 154,256 154,206 H138.5 C138.5,240 121,264 100,266.5 C79,264 61.5,240 61.5,206 Z" fill="url(#ekpo-plait)" stroke="none"/>
    <path d="M53.5,208 C54,248 75,283 100,288 C125,283 146,248 146.5,208" fill="none" stroke-width="1.2" opacity="0.8"/>
  </g>
</g>"""


def svg(body, defs="", title="Ekpo"):
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">
<title>{title}</title>
<defs>{mask_defs()}{defs}</defs>
{body}
</svg>
"""


def mask_at(height, cx=256, cy=256):
    """Places the mask so its visual centre (y=160 of 320) sits at (cx, cy)."""
    scale = height / 320
    return f'<use href="#ekpo-mask" transform="translate({cx - 100 * scale:.2f} {cy - 160 * scale:.2f}) scale({scale:.4f})"/>'


def ring(n, radius, element):
    out = []
    for i in range(n):
        a = 2 * math.pi * i / n
        x, y = 256 + radius * math.sin(a), 256 - radius * math.cos(a)
        out.append(element(x, y, math.degrees(a)))
    return "\n".join(out)


def medallion():
    """A: round badge like an Ankara sunburst medallion."""
    body = f"""
<circle cx="256" cy="256" r="252" fill="{RED}"/>
{ring(56, 236, lambda x, y, a: f'<circle cx="{x:.1f}" cy="{y:.1f}" r="5.5" fill="{YELLOW}"/>')}
<circle cx="256" cy="256" r="222" fill="none" stroke="{YELLOW}" stroke-width="4"/>
{ring(90, 207, lambda x, y, a: f'<rect x="{x - 2:.1f}" y="{y - 9:.1f}" width="4" height="18" rx="2" fill="{YELLOW}" transform="rotate({a:.1f} {x:.1f} {y:.1f})"/>')}
<circle cx="256" cy="256" r="192" fill="{RED_DARK}" stroke="{YELLOW}" stroke-width="4"/>
{mask_at(372)}
"""
    return svg(body, title="Ekpo logo A: medallion")


def sunburst_glyph(x, y):
    """Small wax-print sunburst: a dot ringed by short rays."""
    rays = "".join(
        f'<rect x="{x - 1.2:.1f}" y="{y - 10:.1f}" width="2.4" height="5" rx="1.2" transform="rotate({k * 30} {x} {y})"/>' for k in range(12)
    )
    return f'<circle cx="{x}" cy="{y}" r="3.5"/>{rays}'


def ankara():
    """B: rounded square filled with a wax-print pattern, the mask on a dark halo."""
    defs = f"""
<pattern id="ankara" width="128" height="128" patternUnits="userSpaceOnUse">
  <rect width="128" height="128" fill="{RED}"/>
  <g fill="none" stroke="{YELLOW}">
    <circle cx="32" cy="32" r="26" stroke-width="5"/>
    <circle cx="32" cy="32" r="17" stroke-width="2.5" stroke-dasharray="3 4"/>
    <circle cx="32" cy="32" r="8" fill="{YELLOW}" stroke="none"/>
    <circle cx="96" cy="96" r="26" stroke-width="5"/>
    <circle cx="96" cy="96" r="17" stroke-width="2.5" stroke-dasharray="3 4"/>
    <circle cx="96" cy="96" r="8" fill="{YELLOW}" stroke="none"/>
    <circle cx="96" cy="32" r="13" stroke-width="2.5" stroke-dasharray="1.5 3.2" stroke-linecap="round"/>
    <circle cx="32" cy="96" r="13" stroke-width="2.5" stroke-dasharray="1.5 3.2" stroke-linecap="round"/>
  </g>
  <g fill="{YELLOW}">
    {sunburst_glyph(96, 32)}
    {sunburst_glyph(32, 96)}
  </g>
  <g fill="{YELLOW}">
    <circle cx="64" cy="8" r="3"/><circle cx="8" cy="64" r="3"/><circle cx="64" cy="64" r="4"/><circle cx="120" cy="64" r="3"/><circle cx="64" cy="120" r="3"/>
  </g>
</pattern>"""
    body = f"""
<rect width="512" height="512" rx="112" fill="url(#ankara)"/>
<ellipse cx="256" cy="262" rx="150" ry="228" fill="{RED_DARK}" stroke="{YELLOW}" stroke-width="8"/>
{mask_at(404, cy=262)}
"""
    return svg(body, defs, title="Ekpo logo B: Ankara print")


def sunburst():
    """C: rounded square with yellow rays and a zigzag border."""
    rays = []
    for i in range(32):
        a0, a1 = math.radians(i * 11.25 - 2.2), math.radians(i * 11.25 + 2.2)
        rays.append(f'<path d="M256,256 L{256 + 400 * math.sin(a0):.1f},{256 - 400 * math.cos(a0):.1f} L{256 + 400 * math.sin(a1):.1f},{256 - 400 * math.cos(a1):.1f} Z"/>')
    zig = []
    for side in range(4):
        for k in range(12):
            x0 = 28 + k * 38
            zig.append(f'<path d="M{x0},28 L{x0 + 19},50 L{x0 + 38},28 Z" transform="rotate({side * 90} 256 256)"/>')
    defs = '<clipPath id="tile"><rect width="512" height="512" rx="112"/></clipPath>'
    body = f"""
<g clip-path="url(#tile)">
  <rect width="512" height="512" fill="{RED}"/>
  <g fill="{YELLOW}" opacity="0.85">{"".join(rays)}</g>
  <rect x="14" y="14" width="484" height="484" rx="100" fill="none" stroke="{YELLOW}" stroke-width="6"/>
  <g fill="{YELLOW}">{"".join(zig)}</g>
</g>
<circle cx="256" cy="256" r="176" fill="{RED}" stroke="{YELLOW}" stroke-width="6"/>
<circle cx="256" cy="256" r="163" fill="none" stroke="{YELLOW}" stroke-width="3" stroke-dasharray="2 9" stroke-linecap="round"/>
{mask_at(330)}
"""
    return svg(body, defs, title="Ekpo logo C: sunburst")


def braid():
    """D: rounded square framed by a plaited band like the mask's collar."""
    defs = f"""
<pattern id="dots" width="32" height="32" patternUnits="userSpaceOnUse">
  <circle cx="16" cy="16" r="2.6" fill="{YELLOW}" opacity="0.55"/>
</pattern>
<pattern id="braid" width="36" height="36" patternUnits="userSpaceOnUse">
  <path d="M0,18 C9,4 27,4 36,18 C27,32 9,32 0,18 Z" fill="none" stroke="{YELLOW}" stroke-width="5"/>
  <circle cx="18" cy="18" r="4" fill="{YELLOW}"/>
</pattern>"""
    body = f"""
<rect width="512" height="512" rx="112" fill="{RED}"/>
<path d="M112,0 H400 A112,112 0 0 1 512,112 V400 A112,112 0 0 1 400,512 H112 A112,112 0 0 1 0,400 V112 A112,112 0 0 1 112,0 Z M112,40 A72,72 0 0 0 40,112 V400 A72,72 0 0 0 112,472 H400 A72,72 0 0 0 472,400 V112 A72,72 0 0 0 400,40 Z" fill="url(#braid)" fill-rule="evenodd"/>
<rect x="40" y="40" width="432" height="432" rx="72" fill="url(#dots)" stroke="{YELLOW}" stroke-width="4"/>
<rect x="6" y="6" width="500" height="500" rx="106" fill="none" stroke="{YELLOW}" stroke-width="5"/>
{mask_at(412)}
"""
    return svg(body, defs, title="Ekpo logo D: plaited frame")


def simple():
    """E: the plainest badge, for small sizes (tray, favicon)."""
    body = f"""
<circle cx="256" cy="256" r="252" fill="{RED}"/>
<circle cx="256" cy="256" r="228" fill="none" stroke="{YELLOW}" stroke-width="14"/>
{mask_at(430)}
"""
    return svg(body, title="Ekpo logo E: simple")


def main():
    for name, make in {"a-medallion": medallion, "b-ankara": ankara, "c-sunburst": sunburst, "d-braid": braid, "e-simple": simple}.items():
        (OUT / f"ekpo-logo-{name}.svg").write_text(make())
    (OUT.parent.parent / "packaging" / "io.github.nomoruyi.ekpo.svg").write_text(ankara())


if __name__ == "__main__":
    main()
