#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective
"""Makes the CJK fallback face: mkfont.py FONT PRIMARY STRINGS > OUT.oablf

Converts the characters that the string literals in STRINGS (Ui/Lang.c) use
from the OpenType font FONT to the .oablf format (Gfx/Font.c), at 250 units
per em, one weight. Characters that the primary face PRIMARY (an .oablf) or
ASCII already cover are left out. Fails if FONT lacks a character, so no
string is drawn with missing glyphs. Needs fontTools.
"""
import re
import struct
import sys

from fontTools.pens.basePen import BasePen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.ttLib import TTFont

UNITS_PER_EM = 250

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


def main():
    if len(sys.argv) != 4:
        sys.exit('usage: mkfont.py FONT PRIMARY STRINGS > OUT.oablf')
    font_path, primary_path, strings_path = sys.argv[1:]
    covered = primary_chars(primary_path)
    wanted = {}
    for text in literals(open(strings_path, encoding='utf-8').read()):
        for char in text:
            if ord(char) > 0x7f and ord(char) not in covered:
                wanted.setdefault(ord(char), text)

    font = TTFont(font_path, lazy=True)
    cmap = font.getBestCmap()
    missing = [c for c in sorted(wanted) if c not in cmap or c > 0xffff]
    if missing:
        for c in missing:
            print(f'mkfont.py: {font_path} has no U+{c:04X} {chr(c)!r}, '
                  f'used in "{wanted[c]}"', file=sys.stderr)
        sys.exit(1)

    upm = font['head'].unitsPerEm
    ratio = UNITS_PER_EM / upm
    glyph_set = font.getGlyphSet()
    chars = sorted(wanted)
    records = bytearray()
    outlines = bytearray()
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
        records += struct.pack('<HhhhhI', scale(glyph.width, ratio), x0, y0, x1,
                               y1, len(outlines))
        outlines += pen.data

    hhea = font['hhea']
    header = struct.pack('<4sHHhhhHH', b'OABF', 1, UNITS_PER_EM,
                         scale(hhea.ascent, ratio), scale(hhea.descent, ratio),
                         scale(font['OS/2'].sCapHeight, ratio), len(chars), 1)
    sys.stdout.buffer.write(header + struct.pack(f'<{len(chars)}H', *chars) +
                            records + outlines)


if __name__ == '__main__':
    main()
