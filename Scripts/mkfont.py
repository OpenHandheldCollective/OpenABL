#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective
"""Makes the menu's font faces from OpenType fonts, in the .oablf format
(Gfx/Font.c). Needs fontTools.

  mkfont.py --primary FONT STRINGS... > OUT.oablf

The primary face: printable ASCII, the symbols the menu draws itself (BASE),
and every character below U+2000 that the string literals in the STRINGS
files (Ui/Lang/*.c, one per language) use, so accented Latin, Greek and
Cyrillic match the rest of the menu. Punctuation from U+2000 up (curly
quotes, dashes) is left to the fallback, which draws it as wide as the CJK
strings expect; add any that a non-CJK string needs to BASE. FONT is a
variable font with a wght axis; the face has a Regular (400) and a Medium
(500) weight, at FONT's own units per em.

  mkfont.py FONT PRIMARY STRINGS... > OUT.oablf

The fallback face: the characters those strings use that the primary face
PRIMARY (an .oablf) lacks, at 250 units per em, one weight.

Either fails if FONT lacks a character, so no string is drawn with missing
glyphs.
"""
import re
import struct
import sys

from fontTools.pens.basePen import BasePen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

UNITS_PER_EM = 250

# What the menu draws outside the strings, on top of printable ASCII.
BASE = '°·×…'

# Characters the strings use below this go in the primary face.
PRIMARY_END = 0x2000

# The primary face's weights, as FONT_WEIGHT in Gfx/Gfx.h orders them.
WEIGHTS = (400, 500)

# A C string literal, or a comment to skip.
TOKEN = re.compile(r'"((?:[^"\\\n]|\\.)*)"|//[^\n]*|/\*.*?\*/|\'(?:[^\'\\]|\\.)*\'',
                   re.S)
ESCAPES = {'n': 10, 't': 9, 'r': 13, '0': 0, '\\': 92, '"': 34, "'": 39,
           '?': 63, 'a': 7, 'b': 8, 'f': 12, 'v': 11}


def literals(source):
    """Yields the decoded string literals in C source."""
    for match in TOKEN.finditer(source):
        body = match.group(1)
        if body is None:
            continue
        raw = bytearray()
        at = 0
        while at < len(body):
            if body[at] != '\\':
                raw += body[at].encode()
                at += 1
            elif body[at + 1] == 'x':
                digits = re.match(r'[0-9a-fA-F]+', body[at + 2:]).group()
                raw.append(int(digits, 16) & 0xff)
                at += 2 + len(digits)
            elif body[at + 1] in '01234567':
                digits = re.match(r'[0-7]{1,3}', body[at + 1:]).group()
                raw.append(int(digits, 8) & 0xff)
                at += 1 + len(digits)
            elif body[at + 1] in 'uU':
                digits = body[at + 2:at + (6 if body[at + 1] == 'u' else 10)]
                raw += chr(int(digits, 16)).encode()
                at += 2 + len(digits)
            elif body[at + 1] in ESCAPES:
                raw.append(ESCAPES[body[at + 1]])
                at += 2
            else:
                sys.exit(f'mkfont.py: unknown escape \\{body[at + 1]}')
        yield raw.decode()


def primary_chars(path):
    """Returns the code points of the .oablf face at path."""
    data = open(path, 'rb').read()
    count = struct.unpack_from('<H', data, 14)[0]
    return set(struct.unpack_from(f'<{count}H', data, 18))


def scale(value, ratio):
    """Scales value and rounds half up."""
    return int((value * ratio + 0.5) // 1)


class OutlinePen(BasePen):
    """Encodes an outline as .oablf ops with zigzag LEB128 deltas."""

    def __init__(self, glyph_set, ratio):
        super().__init__(glyph_set)
        self.ratio = ratio
        self.data = bytearray()
        self.x = self.y = 0

    def point(self, pt):
        x, y = scale(pt[0], self.ratio), scale(pt[1], self.ratio)
        for delta in (x - self.x, y - self.y):
            zigzag = (delta << 1) ^ (delta >> 63)
            while True:
                byte = zigzag & 0x7f
                zigzag >>= 7
                self.data.append(byte | (0x80 if zigzag else 0))
                if not zigzag:
                    break
        self.x, self.y = x, y

    def op(self, code, *points):
        self.data.append(code)
        for pt in points:
            self.point(pt)

    def _moveTo(self, pt):
        self.op(0, pt)

    def _lineTo(self, pt):
        self.op(1, pt)

    def _qCurveToOne(self, pt1, pt2):
        self.op(2, pt1, pt2)

    def _curveToOne(self, pt1, pt2, pt3):
        self.op(3, pt1, pt2, pt3)

    def _closePath(self):
        self.data.append(4)

    _endPath = _closePath


def string_chars(strings_paths):
    """Returns {code point: a string that uses it} for the non-ASCII
    characters of the string literals in strings_paths."""
    wanted = {}
    for strings_path in strings_paths:
        for text in literals(open(strings_path, encoding='utf-8').read()):
            for char in text:
                if ord(char) > 0x7f:
                    wanted.setdefault(ord(char), text)
    return wanted


def check(font_path, cmap, wanted):
    """Exits if cmap lacks any of wanted's characters."""
    missing = [c for c in sorted(wanted) if c not in cmap or c > 0xffff]
    if missing:
        for c in missing:
            print(f'mkfont.py: {font_path} has no U+{c:04X} {chr(c)!r}, '
                  f'used in "{wanted[c]}"', file=sys.stderr)
        sys.exit(1)


def face(fonts, chars, units_per_em):
    """Returns the .oablf face of chars, one weight per font in fonts. The
    first font gives the metrics."""
    first = fonts[0]
    ratio = units_per_em / first['head'].unitsPerEm
    records = bytearray()
    outlines = bytearray()
    for font in fonts:
        cmap = font.getBestCmap()
        glyph_set = font.getGlyphSet()
        for char in chars:
            glyph = glyph_set[cmap[char]]
            pen = OutlinePen(glyph_set, ratio)
            glyph.draw(pen)
            pen.data.append(5)
            bounds_pen = BoundsPen(glyph_set)
            glyph.draw(bounds_pen)
            bounds = bounds_pen.bounds or (0, 0, 0, 0)
            x0, y0 = (int((v * ratio) // 1) for v in bounds[:2])
            x1, y1 = (-int((-v * ratio) // 1) for v in bounds[2:])
            records += struct.pack('<HhhhhI', scale(glyph.width, ratio), x0,
                                   y0, x1, y1, len(outlines))
            outlines += pen.data

    hhea = first['hhea']
    header = struct.pack('<4sHHhhhHH', b'OABF', 1, units_per_em,
                         scale(hhea.ascent, ratio), scale(hhea.descent, ratio),
                         scale(first['OS/2'].sCapHeight, ratio), len(chars),
                         len(fonts))
    return (header + struct.pack(f'<{len(chars)}H', *chars) + records +
            outlines)


def primary(font_path, strings_paths):
    """Returns the primary face."""
    wanted = {c: 'the menu' for c in range(0x20, 0x7f)}
    wanted.update((ord(c), 'the menu') for c in BASE)
    for char, text in string_chars(strings_paths).items():
        if char < PRIMARY_END:
            wanted.setdefault(char, text)
    fonts = [instancer.instantiateVariableFont(TTFont(font_path), {'wght': w})
             for w in WEIGHTS]
    check(font_path, fonts[0].getBestCmap(), wanted)
    return face(fonts, sorted(wanted), fonts[0]['head'].unitsPerEm)


def fallback(font_path, primary_path, strings_paths):
    """Returns the fallback face."""
    covered = primary_chars(primary_path)
    wanted = {c: text for c, text in string_chars(strings_paths).items()
              if c not in covered}
    font = TTFont(font_path, lazy=True)
    check(font_path, font.getBestCmap(), wanted)
    return face([font], sorted(wanted), UNITS_PER_EM)


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == '--primary':
        data = primary(sys.argv[2], sys.argv[3:])
    elif len(sys.argv) >= 4 and sys.argv[1] != '--primary':
        data = fallback(sys.argv[1], sys.argv[2], sys.argv[3:])
    else:
        sys.exit('usage: mkfont.py --primary FONT STRINGS... > OUT.oablf\n'
                 '       mkfont.py FONT PRIMARY STRINGS... > OUT.oablf')
    sys.stdout.buffer.write(data)


if __name__ == '__main__':
    main()
