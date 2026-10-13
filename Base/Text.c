/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Text - string handling without a C library.
*/

#include "Base/Text.h"
#include "Base/Memory.h"
#include "Base/Print.h"

BOOLEAN
TextEq (
  IN OABL_TEXT A,
  IN OABL_TEXT B
)
{
  if (A.Len != B.Len) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < A.Len; Index++) {
    if (A.Ptr[Index] != B.Ptr[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
TextEqStr (
  IN OABL_TEXT    A,
  IN CONST CHAR8 *B
)
{
  return TextEq (A, TextOf (B));
}

STATIC CHAR8
Lower (IN CHAR8 Char)
{
  return Char >= 'A' && Char <= 'Z' ? (CHAR8)(Char + 32) : Char;
}

BOOLEAN
TextEqNoCase (
  IN OABL_TEXT    A,
  IN CONST CHAR8 *B
)
{
  UINTN Length = AsciiStrLen (B);
  if (A.Len != Length) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Length; Index++) {
    if (Lower (A.Ptr[Index]) != Lower (B[Index])) {
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
TextStartsWith (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Prefix
)
{
  UINTN Length = AsciiStrLen (Prefix);
  return Length <= Text.Len &&
         TextEq ((OABL_TEXT){Text.Ptr, Length}, (OABL_TEXT){Prefix, Length});
}

BOOLEAN
TextStartsWithNoCase (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Prefix
)
{
  UINTN Length = AsciiStrLen (Prefix);
  return Length <= Text.Len &&
         TextEqNoCase ((OABL_TEXT){Text.Ptr, Length}, Prefix);
}

BOOLEAN
TextEndsWithNoCase (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Suffix
)
{
  UINTN Length = AsciiStrLen (Suffix);

  return Text.Len >= Length &&
         TextEqNoCase (TextSub (Text, Text.Len - Length, Text.Len), Suffix);
}

BOOLEAN
TextEndsWith (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Suffix
)
{
  UINTN Length = AsciiStrLen (Suffix);
  return Length <= Text.Len &&
         TextEq (
           (OABL_TEXT){Text.Ptr + Text.Len - Length, Length},
           (OABL_TEXT){Suffix, Length}
         );
}

BOOLEAN
TextFind (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Needle,
  OUT UINTN      *At
)
{
  UINTN Length = AsciiStrLen (Needle);
  if (Length > Text.Len) {
    return FALSE;
  }
  for (UINTN Index = 0; Index + Length <= Text.Len; Index++) {
    if (TextEq (
          (OABL_TEXT){Text.Ptr + Index, Length}, (OABL_TEXT){Needle, Length}
        )) {
      *At = Index;
      return TRUE;
    }
  }
  return FALSE;
}

BOOLEAN
TextFindChar (
  IN OABL_TEXT Text,
  IN CHAR8     Char,
  OUT UINTN   *At
)
{
  for (UINTN Index = 0; Index < Text.Len; Index++) {
    if (Text.Ptr[Index] == Char) {
      *At = Index;
      return TRUE;
    }
  }
  return FALSE;
}

BOOLEAN
TextFindLastChar (
  IN OABL_TEXT Text,
  IN CHAR8     Char,
  OUT UINTN   *At
)
{
  for (UINTN Index = Text.Len; Index--;) {
    if (Text.Ptr[Index] == Char) {
      *At = Index;
      return TRUE;
    }
  }
  return FALSE;
}

BOOLEAN
IsUnicodeWhitespace (IN UINT32 CodePoint)
{
  return (CodePoint >= 0x09 && CodePoint <= 0x0d) || CodePoint == 0x20 ||
         CodePoint == 0x85 || CodePoint == 0xa0 || CodePoint == 0x1680 ||
         (CodePoint >= 0x2000 && CodePoint <= 0x200a) || CodePoint == 0x2028 ||
         CodePoint == 0x2029 || CodePoint == 0x202f || CodePoint == 0x205f ||
         CodePoint == 0x3000;
}

// Returns the length of a whitespace character at the start of Text, or 0.
STATIC UINTN
SpaceAt (IN OABL_TEXT Text)
{
  UINTN Length;
  if (!Text.Len) {
    return 0;
  }
  if ((UINT8)*Text.Ptr < 0x80) {
    return IsUnicodeWhitespace ((UINT8)*Text.Ptr) ? 1 : 0;
  }
  return IsUnicodeWhitespace (Utf8Decode (Text.Ptr, Text.Len, &Length)) ? Length
                                                                        : 0;
}

OABL_TEXT
TextTrimStart (IN OABL_TEXT Text)
{
  UINTN Length;
  while ((Length = SpaceAt (Text))) {
    Text.Ptr += Length;
    Text.Len -= Length;
  }
  return Text;
}

OABL_TEXT
TextTrimEnd (IN OABL_TEXT Text)
{
  while (Text.Len) {
    // The last character starts at the last non-continuation byte, at most
    // 3 bytes back.
    UINTN Start = Text.Len - 1;
    UINTN Length;
    while (Start > 0 && Text.Len - Start < 4 &&
           ((UINT8)Text.Ptr[Start] & 0xc0) == 0x80) {
      Start--;
    }
    Length = SpaceAt (TextSub (Text, Start, Text.Len));
    if (!Length || Start + Length != Text.Len) {
      break;
    }
    Text.Len = Start;
  }
  return Text;
}

OABL_TEXT
TextTrim (IN OABL_TEXT Text)
{
  return TextTrimEnd (TextTrimStart (Text));
}

BOOLEAN
TextSplitOnce (
  IN OABL_TEXT   Text,
  IN CHAR8       Separator,
  OUT OABL_TEXT *Before,
  OUT OABL_TEXT *After
)
{
  UINTN At;
  if (!TextFindChar (Text, Separator, &At)) {
    return FALSE;
  }
  *Before = (OABL_TEXT){Text.Ptr, At};
  *After  = (OABL_TEXT){Text.Ptr + At + 1, Text.Len - At - 1};
  return TRUE;
}

BOOLEAN
TextNextLine (
  IN OUT OABL_TEXT *Rest,
  OUT OABL_TEXT    *Line
)
{
  UINTN At;
  if (!Rest->Len) {
    return FALSE;
  }
  if (TextFindChar (*Rest, '\n', &At)) {
    *Line = (OABL_TEXT){Rest->Ptr, At};
    Rest->Ptr += At + 1;
    Rest->Len -= At + 1;
    // Only a \r before a \n is dropped. A last line without a \n keeps a
    // bare \r.
    if (Line->Len && Line->Ptr[Line->Len - 1] == '\r') {
      Line->Len--;
    }
  } else {
    *Line = *Rest;
    Rest->Ptr += Rest->Len;
    Rest->Len = 0;
  }
  return TRUE;
}

BOOLEAN
TextNextWord (
  IN OUT OABL_TEXT *Rest,
  OUT OABL_TEXT    *Word
)
{
  UINTN End = 0;
  UINTN Length;
  *Rest = TextTrimStart (*Rest);
  if (!Rest->Len) {
    return FALSE;
  }
  while (End < Rest->Len && !SpaceAt (TextSub (*Rest, End, Rest->Len))) {
    Utf8Decode (Rest->Ptr + End, Rest->Len - End, &Length);
    End += Length;
  }
  *Word = (OABL_TEXT){Rest->Ptr, End};
  Rest->Ptr += End;
  Rest->Len -= End;
  return TRUE;
}

BOOLEAN
TextNextSplit (
  IN OUT OABL_TEXT *Rest,
  IN CHAR8          Separator,
  IN OUT BOOLEAN   *Done,
  OUT OABL_TEXT    *Piece
)
{
  UINTN At;
  if (*Done) {
    return FALSE;
  }
  if (TextFindChar (*Rest, Separator, &At)) {
    *Piece = (OABL_TEXT){Rest->Ptr, At};
    Rest->Ptr += At + 1;
    Rest->Len -= At + 1;
  } else {
    *Piece = *Rest;
    Rest->Ptr += Rest->Len;
    Rest->Len = 0;
    *Done     = TRUE;
  }
  return TRUE;
}

BOOLEAN
TextParseU64 (
  IN OABL_TEXT Text,
  IN UINT32    Base,
  OUT UINT64  *Value
)
{
  UINT64 Number = 0;
  if (Text.Len && *Text.Ptr == '+') {
    Text.Ptr++;
    Text.Len--;
  }
  if (!Text.Len) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Text.Len; Index++) {
    CHAR8  Char = Text.Ptr[Index];
    UINT32 Digit;
    if (Char >= '0' && Char <= '9') {
      Digit = (UINT32)(Char - '0');
    } else if (Base == 16 && Lower (Char) >= 'a' && Lower (Char) <= 'f') {
      Digit = (UINT32)(Lower (Char) - 'a' + 10);
    } else {
      return FALSE;
    }
    if (Digit >= Base || Number > (MAX_UINT64 - Digit) / Base) {
      return FALSE;
    }
    Number = Number * Base + Digit;
  }
  *Value = Number;
  return TRUE;
}

CHAR8 *
TextDup (IN OABL_TEXT Text)
{
  CHAR8 *Copy = AllocatePool (Text.Len + 1);
  if (!Copy) {
    return NULL;
  }
  for (UINTN Index = 0; Index < Text.Len; Index++) {
    Copy[Index] = Text.Ptr[Index];
  }
  Copy[Text.Len] = 0;
  return Copy;
}

CHAR8 *
AsciiStrDup (IN CONST CHAR8 *String OPTIONAL)
{
  return TextDup (TextOf (String ? String : ""));
}

// Growable string

// Makes room for Extra more characters and the terminator. Once an
// allocation fails, Failed is set and every later call fails.
STATIC BOOLEAN
MakeRoom (
  IN OUT OABL_STRING *Builder,
  IN UINTN            Extra
)
{
  UINTN  Need = Builder->Len + Extra + 1;
  UINTN  Capacity;
  CHAR8 *Grown;

  if (Builder->Failed) {
    return FALSE;
  }
  if (Need <= Builder->Cap) {
    return TRUE;
  }
  Capacity = Builder->Cap ? Builder->Cap * 2 : 32;
  while (Capacity < Need) {
    Capacity *= 2;
  }
  Grown = ResizePool (Builder->Str, Capacity);
  if (!Grown) {
    Builder->Failed = TRUE;
    return FALSE;
  }
  Builder->Str = Grown;
  Builder->Cap = Capacity;
  return TRUE;
}

VOID
StringAppendN (
  IN OUT OABL_STRING *Builder,
  IN CONST CHAR8     *String,
  IN UINTN            Length
)
{
  if (!MakeRoom (Builder, Length)) {
    return;
  }
  for (UINTN Index = 0; Index < Length; Index++) {
    Builder->Str[Builder->Len++] = String[Index];
  }
  Builder->Str[Builder->Len] = 0;
}

VOID
StringAppend (
  IN OUT OABL_STRING *Builder,
  IN CONST CHAR8     *String
)
{
  StringAppendN (Builder, String, AsciiStrLen (String));
}

VOID
StringAppendChar (
  IN OUT OABL_STRING *Builder,
  IN CHAR8            Char
)
{
  StringAppendN (Builder, &Char, 1);
}

VOID
StringAppendCodePoint (
  IN OUT OABL_STRING *Builder,
  IN UINT32           CodePoint
)
{
  CHAR8 Utf8[4];
  UINTN Length;

  if (CodePoint < 0x80) {
    Utf8[0] = (CHAR8)CodePoint;
    Length  = 1;
  } else if (CodePoint < 0x800) {
    Utf8[0] = (CHAR8)(0xc0 | CodePoint >> 6);
    Utf8[1] = (CHAR8)(0x80 | (CodePoint & 0x3f));
    Length  = 2;
  } else if (CodePoint < 0x10000) {
    Utf8[0] = (CHAR8)(0xe0 | CodePoint >> 12);
    Utf8[1] = (CHAR8)(0x80 | (CodePoint >> 6 & 0x3f));
    Utf8[2] = (CHAR8)(0x80 | (CodePoint & 0x3f));
    Length  = 3;
  } else {
    Utf8[0] = (CHAR8)(0xf0 | CodePoint >> 18);
    Utf8[1] = (CHAR8)(0x80 | (CodePoint >> 12 & 0x3f));
    Utf8[2] = (CHAR8)(0x80 | (CodePoint >> 6 & 0x3f));
    Utf8[3] = (CHAR8)(0x80 | (CodePoint & 0x3f));
    Length  = 4;
  }
  StringAppendN (Builder, Utf8, Length);
}

VOID
StringVPrint (
  IN OUT OABL_STRING *Builder,
  IN CONST CHAR8     *Format,
  IN va_list          Args
)
{
  va_list Again;
  INT32   Length;

  va_copy (Again, Args);
  Length = VSPrint (NULL, 0, Format, Args);
  if (Length > 0 && MakeRoom (Builder, (UINTN)Length)) {
    VSPrint (Builder->Str + Builder->Len, (UINTN)Length + 1, Format, Again);
    Builder->Len += (UINTN)Length;
  }
  va_end (Again);
}

VOID
StringPrint (
  IN OUT OABL_STRING *Builder,
  IN CONST CHAR8     *Format,
  ...
)
{
  va_list Args;
  va_start (Args, Format);
  StringVPrint (Builder, Format, Args);
  va_end (Args);
}

CHAR8 *
StringTake (IN OUT OABL_STRING *Builder)
{
  CHAR8 *Text = Builder->Str;
  if (!Text) {
    Text = AllocateZeroPool (1);
  }
  *Builder = (OABL_STRING){0};
  return Text;
}

VOID
StringFree (IN OUT OABL_STRING *Builder)
{
  FreePool (Builder->Str);
  *Builder = (OABL_STRING){0};
}

VOID
StringClear (IN OUT OABL_STRING *Builder)
{
  Builder->Len    = 0;
  Builder->Failed = FALSE;
  if (Builder->Str) {
    Builder->Str[0] = 0;
  }
}

// UTF-8 and UTF-16

UINT32
Utf8Decode (
  IN CONST CHAR8 *Utf8,
  IN UINTN        Size,
  OUT UINTN      *Length
)
{
  CONST UINT8 *Bytes = (CONST UINT8 *)Utf8;
  UINT8        Lead  = Bytes[0];
  UINT8        Low   = 0x80;
  UINT8        High  = 0xbf;
  UINT32       CodePoint;
  UINTN        Need;

  *Length = 1;
  if (Lead < 0x80) {
    return Lead;
  }
  if (Lead >= 0xc2 && Lead <= 0xdf) {
    Need      = 1;
    CodePoint = Lead & 0x1f;
  } else if (Lead >= 0xe0 && Lead <= 0xef) {
    Need      = 2;
    CodePoint = Lead & 0x0f;
    if (Lead == 0xe0) {
      Low = 0xa0;
    } else if (Lead == 0xed) {
      High = 0x9f;
    }
  } else if (Lead >= 0xf0 && Lead <= 0xf4) {
    Need      = 3;
    CodePoint = Lead & 0x07;
    if (Lead == 0xf0) {
      Low = 0x90;
    } else if (Lead == 0xf4) {
      High = 0x8f;
    }
  } else {
    return 0xfffd;
  }
  // The bytes that fit the sequence so far are part of the replacement.
  for (UINTN Index = 1; Index <= Need; Index++) {
    if (Index >= Size || Bytes[Index] < Low || Bytes[Index] > High) {
      *Length = Index;
      return 0xfffd;
    }
    CodePoint = CodePoint << 6 | (Bytes[Index] & 0x3f);
    Low       = 0x80;
    High      = 0xbf;
  }
  *Length = Need + 1;
  return CodePoint;
}

BOOLEAN
Utf8Valid (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
)
{
  UINTN At = 0;
  UINTN Length;
  while (At < Size) {
    // A literal U+FFFD in the text is valid.
    if (Utf8Decode ((CONST CHAR8 *)Bytes + At, Size - At, &Length) == 0xfffd &&
        !(Length == 3 && Size - At >= 3 && Bytes[At] == 0xef &&
          Bytes[At + 1] == 0xbf && Bytes[At + 2] == 0xbd)) {
      return FALSE;
    }
    At += Length;
  }
  return TRUE;
}

CHAR8 *
Utf8Lossy (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
)
{
  OABL_STRING Builder = {0};
  UINTN       At      = 0;
  UINTN       Length;

  while (At < Size) {
    UINT32 CodePoint =
      Utf8Decode ((CONST CHAR8 *)Bytes + At, Size - At, &Length);
    if (CodePoint == 0xfffd) {
      StringAppendCodePoint (&Builder, CodePoint);
    } else {
      StringAppendN (&Builder, (CONST CHAR8 *)Bytes + At, Length);
    }
    At += Length;
  }
  return Builder.Failed ? (StringFree (&Builder), NULL) : StringTake (&Builder);
}

UINT16 *
Utf16FromText (
  IN OABL_TEXT Text,
  OUT UINTN   *Units OPTIONAL
)
{
  UINT16 *Utf16 = AllocatePool ((Text.Len + 1) * sizeof (*Utf16));
  UINTN   At    = 0;
  UINTN   Count = 0;
  UINTN   Length;

  if (!Utf16) {
    return NULL;
  }
  while (At < Text.Len) {
    UINT32 CodePoint = Utf8Decode (Text.Ptr + At, Text.Len - At, &Length);
    if (CodePoint >= 0x10000) {
      CodePoint -= 0x10000;
      Utf16[Count++] = (UINT16)(0xd800 | CodePoint >> 10);
      Utf16[Count++] = (UINT16)(0xdc00 | (CodePoint & 0x3ff));
    } else {
      Utf16[Count++] = (UINT16)CodePoint;
    }
    At += Length;
  }
  Utf16[Count] = 0;
  if (Units) {
    *Units = Count;
  }
  return Utf16;
}

UINTN
Utf16Len (IN CONST UINT16 *String)
{
  UINTN Length = 0;
  // Without volatile the compiler turns this loop into a wcslen call, and
  // there is no C library to provide it.
  while (((CONST volatile UINT16 *)String)[Length]) {
    Length++;
  }
  return Length;
}

CHAR8 *
Utf16ToUtf8 (
  IN CONST UINT16 *Utf16,
  IN UINTN         Count
)
{
  OABL_STRING Builder = {0};

  for (UINTN Index = 0; Index < Count; Index++) {
    UINT32 CodePoint = Utf16[Index];
    if (CodePoint >= 0xd800 && CodePoint <= 0xdbff && Index + 1 < Count &&
        Utf16[Index + 1] >= 0xdc00 && Utf16[Index + 1] <= 0xdfff) {
      CodePoint =
        0x10000 + ((CodePoint - 0xd800) << 10) + (Utf16[Index + 1] - 0xdc00u);
      Index++;
    } else if (CodePoint >= 0xd800 && CodePoint <= 0xdfff) {
      CodePoint = 0xfffd;
    }
    StringAppendCodePoint (&Builder, CodePoint);
  }
  return Builder.Failed ? (StringFree (&Builder), NULL) : StringTake (&Builder);
}
