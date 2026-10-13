/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Text - the menu font laid out: advances in whole pixels, no kerning, word
  wrap and an ellipsis for text that doesn't fit.
*/

#include "View/Text.h"

#include "Gfx/Gfx.h"

#define MAX_FONTS 48
#define NO_BREAK  0xffffffffu
// What lines break after.
#define BREAK_CHARS " ,.;:-_)]}/"

STATIC TEXT_FONT mFonts[MAX_FONTS];
STATIC UINT32    mKeys[MAX_FONTS];
STATIC UINTN     mFontCount;

CONST TEXT_FONT *
TextFont (
  IN float       Px,
  IN FONT_WEIGHT Weight
)
{
  UINT32     Key = (UINT32)(Px * 4.0f + 0.5f) | (UINT32)Weight << 24;
  TEXT_FONT *Font;

  for (UINTN Index = 0; Index < mFontCount; Index++) {
    if (mKeys[Index] == Key) {
      return &mFonts[Index];
    }
  }
  if (mFontCount == MAX_FONTS) {
    TEXT_FONT *Near = &mFonts[0];

    for (UINTN Index = 1; Index < mFontCount; Index++) {
      Font = &mFonts[Index];
      if ((Near->Weight != Weight && Font->Weight == Weight) ||
          (Font->Weight == Near->Weight &&
           FabsF (Font->Px - Px) < FabsF (Near->Px - Px))) {
        Near = Font;
      }
    }
    return Near;
  }
  mKeys[mFontCount] = Key;
  Font              = &mFonts[mFontCount++];
  Font->Px          = Px;
  Font->Weight      = Weight;
  Font->LineH       = (INT32)CeilF (FontAscender (Px) - FontDescender (Px));
  Font->Base        = (INT32)CeilF (-FontDescender (Px));
  return Font;
}

VOID
TextFontsReset (VOID)
{
  mFontCount = 0;
}

// Returns the advance of Letter in whole pixels.
STATIC INT32
GlyphW (
  IN CONST TEXT_FONT *Font,
  IN UINT32           Letter
)
{
  CONST FONT_GLYPH *Glyph = FontGlyph (Font->Px, Font->Weight, Letter, FALSE);

  return Glyph ? (INT32)RoundF (Glyph->Advance) : 0;
}

STATIC BOOLEAN
IsHangingSpace (IN UINT32 Letter)
{
  return Letter == ' ' || Letter == '\n' || Letter == '\r';
}

STATIC BOOLEAN
IsBreakChar (IN UINT32 Letter)
{
  for (CONST CHAR8 *Char = BREAK_CHARS; *Char; Char++) {
    if (Letter == (UINT32)*Char) {
      return TRUE;
    }
  }
  return FALSE;
}

// CJK and fullwidth characters, each a word of its own.
STATIC BOOLEAN
IsAWord (IN UINT32 Letter)
{
  return (Letter >= 0x4E00 && Letter <= 0x9FFF) ||
         (Letter >= 0xFF01 && Letter <= 0xFF5E) ||
         (Letter >= 0x3000 && Letter <= 0x303F) ||
         (Letter >= 0x2E80 && Letter <= 0x2EFF) ||
         (Letter >= 0x31C0 && Letter <= 0x31EF) ||
         (Letter >= 0x3040 && Letter <= 0x30FF) ||
         (Letter >= 0xFE10 && Letter <= 0xFE1F) ||
         (Letter >= 0xFE30 && Letter <= 0xFE4F);
}

// Decodes the letter at *Index and moves *Index past it. The NUL counts as
// a letter.
STATIC UINT32
NextLetter (
  IN CONST CHAR8 *String,
  IN OUT UINT32  *Index
)
{
  UINTN  Length;
  UINT32 Letter = Utf8Next (String + *Index, &Length);

  *Index += (UINT32)Length;
  return Letter;
}

INT32
TextWidth (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN UINTN            Length
)
{
  UINTN  End   = 0;
  INT32  Width = 0;
  UINT32 Index = 0;

  while (End < Length && String[End]) {
    End++;
  }
  while (End > 0 && IsHangingSpace ((UINT8)String[End - 1])) {
    End--;
  }
  while (Index < End) {
    Width += GlyphW (Font, NextLetter (String, &Index));
  }
  return Width;
}

VOID
TextFit (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN INT32            MaxW,
  OUT CHAR8          *Out,
  IN UINTN            Cap
)
{
  STATIC CONST CHAR8 Ellipsis[] = "\xe2\x80\xa6";
  UINTN              Length     = 0;
  INT32              Width      = 0;
  INT32              EllipsisW;

  if (TextWidth (Font, String, MAX_UINTN) <= MaxW) {
    while (*String && Length + 1 < Cap) {
      Out[Length++] = *String++;
    }
    Out[Length] = 0;
    return;
  }
  EllipsisW = TextWidth (Font, Ellipsis, MAX_UINTN);
  while (*String) {
    UINTN  Bytes;
    UINT32 Letter  = Utf8Next (String, &Bytes);
    INT32  LetterW = GlyphW (Font, Letter);
    if (Width + LetterW + EllipsisW > MaxW ||
        Length + Bytes + sizeof (Ellipsis) > Cap) {
      break;
    }
    for (UINTN Index = 0; Index < Bytes; Index++) {
      Out[Length++] = String[Index];
    }
    Width += LetterW;
    String += Bytes;
  }
  while (Length && Out[Length - 1] == ' ') {
    Length--;
  }
  for (UINTN Index = 0; Ellipsis[Index] && Length + 1 < Cap; Index++) {
    Out[Length++] = Ellipsis[Index];
  }
  Out[Length] = 0;
}

// Returns the offset of the next word in String, measuring it in *WordW.
// A word that doesn't fit MaxW returns 0, or with BreakAll is cut where it
// stops fitting.
STATIC UINT32
NextWord (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN INT32            MaxW,
  IN BOOLEAN          BreakAll,
  OUT UINT32         *WordW
)
{
  UINT32 Index      = 0;
  UINT32 Next       = 0;
  UINT32 NextNext   = 0;
  UINT32 Letter     = 0;
  UINT32 LetterNext = 0;
  INT32  Width      = 0;
  UINT32 WordLen    = 0;
  UINT32 BreakAt    = NO_BREAK;

  if (!String[0]) {
    return 0;
  }
  Letter   = NextLetter (String, &Next);
  NextNext = Next;
  while (String[Index]) {
    LetterNext = NextLetter (String, &NextNext);
    WordLen++;
    Width += GlyphW (Font, Letter);

    BOOLEAN IsBreak = Letter == '\n' || Letter == '\r' || IsBreakChar (Letter);
    if (!IsHangingSpace (Letter) && BreakAt == NO_BREAK && Width > MaxW) {
      BreakAt = Index;
      if (BreakAll) {
        break;
      }
    }
    if (IsBreak) {
      if (Index == 0 && BreakAt == NO_BREAK) {
        *WordW = (UINT32)Width;
      }
      WordLen--;
      break;
    } else if (IsAWord (LetterNext) || IsAWord (Letter)) {
      *WordW = (UINT32)Width;
      Index  = Next;
      break;
    }
    if (BreakAt == NO_BREAK) {
      *WordW = (UINT32)Width;
    }
    Index  = Next;
    Next   = NextNext;
    Letter = LetterNext;
  }
  if (BreakAt == NO_BREAK) {
    if (WordLen == 0 || (Letter == '\r' && LetterNext == '\n')) {
      Index = Next;
    }
    return Index;
  }
  if (BreakAll) {
    return BreakAt;
  }
  *WordW = 0;
  return 0;
}

STATIC BOOLEAN
IsSpaceRun (
  IN CONST CHAR8 *String,
  IN UINT32       Length
)
{
  for (UINT32 Index = 0; Index < Length; Index++) {
    if (String[Index] != ' ') {
      return FALSE;
    }
  }
  return Length > 0;
}

// Returns the length of the line String starts, including the spaces it
// ends on. With BreakAll every word may be cut.
STATIC UINT32
NextLine (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN INT32            MaxW,
  IN BOOLEAN          BreakAll
)
{
  UINT32  Index    = 0;
  UINT32  Room     = (UINT32)MaxW;
  UINT32  Hanging  = 0;
  BOOLEAN Explicit = FALSE;

  if (!String[0]) {
    return 0;
  }
  while (String[Index] && Room > Hanging) {
    UINT32 WordW   = 0;
    UINT32 Advance = NextWord (
      Font, &String[Index], (INT32)(Room - Hanging), BreakAll || Index == 0,
      &WordW
    );

    if (Advance == 0) {
      break;
    }
    // A space counts only if a word follows it on the line.
    if (IsSpaceRun (&String[Index], Advance)) {
      Hanging += WordW;
    } else {
      Room -= WordW + Hanging;
      Hanging = 0;
    }
    Index += Advance;
    if (String[0] == '\n' || String[0] == '\r') {
      Explicit = TRUE;
      break;
    }
    if (String[Index] == '\n' || String[Index] == '\r') {
      Index++;
      Explicit = TRUE;
      break;
    }
  }
  if (Index == 0) {
    NextLetter (String, &Index);
  }
  if (!Explicit) {
    while (String[Index] == ' ') {
      Index++;
    }
  }
  return Index;
}

UINTN
TextWrap (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN INT32            MaxW,
  OUT TEXT_SPAN      *Lines OPTIONAL,
  IN UINTN            Max
)
{
  UINTN  Count = 0;
  UINT32 Start = 0;

  while (String[Start]) {
    UINT32 Length = NextLine (Font, String + Start, MaxW, FALSE);
    if (Lines && Count < Max) {
      Lines[Count] = (TEXT_SPAN){Start, Length};
    }
    Count++;
    Start += Length;
  }
  return Count;
}

// Returns the offset of the letter in Length bytes of String under X, or
// of the last letter.
STATIC UINT32
LetterAt (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN UINT32           Length,
  IN INT32            X
)
{
  UINT32 Index = 0;
  INT32  Left  = 0;

  while (Index < Length && String[Index]) {
    UINT32 At     = Index;
    INT32  Letter = GlyphW (Font, NextLetter (String, &Index));
    if (X < Left + Letter || Index >= Length || !String[Index]) {
      return At;
    }
    Left += Letter;
  }
  return Index;
}

UINTN
TextDotsAt (
  IN CONST TEXT_FONT *Font,
  IN CONST CHAR8     *String,
  IN INT32            MaxW,
  IN UINTN            MaxLines
)
{
  UINT32 Start = 0;
  UINT32 Total = 0;
  UINT32 At;

  for (UINTN Line = 0; Line + 1 < MaxLines && String[Start]; Line++) {
    Start += NextLine (Font, String + Start, MaxW, FALSE);
  }
  At =
    Start + LetterAt (
              Font, String + Start, NextLine (Font, String + Start, MaxW, TRUE),
              MaxW - 3 * GlyphW (Font, '.')
            );
  while (String[Total]) {
    Total++;
  }
  // Keep room for the dots.
  while (At > 0 && At + 3 > Total) {
    At--;
    while (At > 0 && ((UINT8)String[At] & 0xc0) == 0x80) {
      At--;
    }
  }
  return At;
}

BOOLEAN
TextBounds (
  IN CONST TEXT_FONT *Font,
  IN INT32            X,
  IN INT32            Top,
  IN CONST CHAR8     *String,
  IN UINTN            Length,
  OUT INT32          *X0,
  OUT INT32          *Y0,
  OUT INT32          *X1,
  OUT INT32          *Y1
)
{
  INT32   Baseline = Top + Font->LineH - Font->Base;
  BOOLEAN Any      = FALSE;

  for (UINTN Index = 0; Index < Length && String[Index];) {
    UINTN             Bytes;
    CONST FONT_GLYPH *Glyph = FontGlyph (
      Font->Px, Font->Weight, Utf8Next (String + Index, &Bytes), FALSE
    );

    if (Glyph && Glyph->W && Glyph->H) {
      INT32 Left   = X + Glyph->Left;
      INT32 Upper  = Baseline + Glyph->Top;
      INT32 Right  = Left + Glyph->W;
      INT32 Bottom = Upper + Glyph->H;
      if (!Any) {
        *X0 = Left, *Y0 = Upper, *X1 = Right, *Y1 = Bottom;
        Any = TRUE;
      } else {
        *X0 = Left < *X0 ? Left : *X0;
        *Y0 = Upper < *Y0 ? Upper : *Y0;
        *X1 = Right > *X1 ? Right : *X1;
        *Y1 = Bottom > *Y1 ? Bottom : *Y1;
      }
    }
    X += Glyph ? (INT32)RoundF (Glyph->Advance) : 0;
    Index += Bytes;
  }
  return Any;
}
