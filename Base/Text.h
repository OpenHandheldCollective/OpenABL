/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Text - string handling without a C library.

  Bounded copies into fixed buffers, borrowed text (pointer and length), a
  growable string, and UTF-8/UTF-16 conversion. Writes to a fixed buffer
  always leave it terminated. Functions returning CHAR8 * allocate from the
  pool and return NULL only when memory runs out. Caller frees the result.
*/

#ifndef __BASE_TEXT_H__
#define __BASE_TEXT_H__

#include <stdarg.h>
#include "Base/OablBase.h"

STATIC inline UINTN
AsciiStrLen (IN CONST CHAR8 *String)
{
  UINTN Length = 0;
  while (String[Length]) {
    Length++;
  }
  return Length;
}

STATIC inline BOOLEAN
AsciiStrEq (
  IN CONST CHAR8 *A,
  IN CONST CHAR8 *B
)
{
  while (*A && *A == *B) {
    A++;
    B++;
  }
  return *A == *B;
}

// Appends String to Buffer, truncating it if it doesn't fit.
STATIC inline VOID
AsciiStrAppend (
  IN OUT CHAR8   *Buffer,
  IN UINTN        BufferSize,
  IN CONST CHAR8 *String
)
{
  UINTN Length = AsciiStrLen (Buffer);
  while (*String && Length + 1 < BufferSize) {
    Buffer[Length++] = *String++;
  }
  if (BufferSize) {
    Buffer[Length < BufferSize ? Length : BufferSize - 1] = 0;
  }
}

// Copies String into Buffer, truncating it if it doesn't fit.
STATIC inline VOID
AsciiStrCopy (
  OUT CHAR8      *Buffer,
  IN UINTN        BufferSize,
  IN CONST CHAR8 *String
)
{
  if (!BufferSize) {
    return;
  }
  Buffer[0] = 0;
  AsciiStrAppend (Buffer, BufferSize, String);
}

STATIC inline VOID
AsciiStrFromU64 (
  OUT CHAR8 *Buffer,
  IN UINTN   BufferSize,
  IN UINT64  Value
)
{
  CHAR8 Digits[21];
  UINTN At   = sizeof (Digits) - 1;
  Digits[At] = 0;
  do {
    Digits[--At] = (CHAR8)('0' + Value % 10);
    Value /= 10;
  } while (Value);
  AsciiStrCopy (Buffer, BufferSize, &Digits[At]);
}

// Borrowed text and bytes. They are not terminated.

typedef struct _OABL_TEXT {
  CONST CHAR8 *Ptr;
  UINTN        Len;
} OABL_TEXT;

typedef struct _OABL_BYTES {
  CONST UINT8 *Ptr;
  UINTN        Len;
} OABL_BYTES;

// NULL gives empty text.
STATIC inline OABL_TEXT
TextOf (IN CONST CHAR8 *String OPTIONAL)
{
  return (OABL_TEXT){String, String ? AsciiStrLen (String) : 0};
}

STATIC inline OABL_BYTES
BytesOf (
  IN CONST VOID *Data,
  IN UINTN       Length
)
{
  return (OABL_BYTES){(CONST UINT8 *)Data, Length};
}

// Returns Text from byte offset From to To, clamped to its length.
STATIC inline OABL_TEXT
TextSub (
  IN OABL_TEXT Text,
  IN UINTN     From,
  IN UINTN     To
)
{
  if (To > Text.Len) {
    To = Text.Len;
  }
  if (From > To) {
    From = To;
  }
  return (OABL_TEXT){Text.Ptr + From, To - From};
}

BOOLEAN
TextEq (
  IN OABL_TEXT A,
  IN OABL_TEXT B
);

BOOLEAN
TextEqStr (
  IN OABL_TEXT    A,
  IN CONST CHAR8 *B
);

// Compares ASCII letters without regard to case.
BOOLEAN
TextEqNoCase (
  IN OABL_TEXT    A,
  IN CONST CHAR8 *B
);

BOOLEAN
TextStartsWith (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Prefix
);

BOOLEAN
TextEndsWith (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Suffix
);

BOOLEAN
TextStartsWithNoCase (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Prefix
);

BOOLEAN
TextEndsWithNoCase (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Suffix
);

BOOLEAN
TextFind (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Needle,
  OUT UINTN      *At
);

BOOLEAN
TextFindChar (
  IN OABL_TEXT Text,
  IN CHAR8     Char,
  OUT UINTN   *At
);

BOOLEAN
TextFindLastChar (
  IN OABL_TEXT Text,
  IN CHAR8     Char,
  OUT UINTN   *At
);

STATIC inline BOOLEAN
TextContains (
  IN OABL_TEXT    Text,
  IN CONST CHAR8 *Needle
)
{
  UINTN At;
  return TextFind (Text, Needle, &At);
}

BOOLEAN
IsUnicodeWhitespace (IN UINT32 CodePoint);

// Space, tab, LF, FF or CR. Vertical tab doesn't count.
STATIC inline BOOLEAN
IsAsciiSpace (IN CHAR8 Char)
{
  return Char == ' ' || Char == '\t' || Char == '\n' || Char == '\f' ||
         Char == '\r';
}

// Trims Unicode White_Space. TextTrimStart and TextTrimEnd trim one end.
OABL_TEXT
TextTrim (IN OABL_TEXT Text);

OABL_TEXT
TextTrimStart (IN OABL_TEXT Text);

OABL_TEXT
TextTrimEnd (IN OABL_TEXT Text);

// Splits Text at the first Separator. Fails if there is none.
BOOLEAN
TextSplitOnce (
  IN OABL_TEXT   Text,
  IN CHAR8       Separator,
  OUT OABL_TEXT *Before,
  OUT OABL_TEXT *After
);

// Takes the next line from Rest. A \r before the \n is dropped, and a final
// \n doesn't start an empty line.
BOOLEAN
TextNextLine (
  IN OUT OABL_TEXT *Rest,
  OUT OABL_TEXT    *Line
);

// Takes the next whitespace-separated word from Rest.
BOOLEAN
TextNextWord (
  IN OUT OABL_TEXT *Rest,
  OUT OABL_TEXT    *Word
);

// Takes the next piece of Rest up to Separator. Empty pieces are kept and
// an empty string is one piece. Set Done to FALSE before the first call.
BOOLEAN
TextNextSplit (
  IN OUT OABL_TEXT *Rest,
  IN CHAR8          Separator,
  IN OUT BOOLEAN   *Done,
  OUT OABL_TEXT    *Piece
);

// Parses an unsigned number in base 10 or 16, with an optional '+'. Fails
// on any other character, on no digits and on overflow.
BOOLEAN
TextParseU64 (
  IN OABL_TEXT Text,
  IN UINT32    Base,
  OUT UINT64  *Value
);

// Returns a terminated copy. AsciiStrDup copies NULL as "".
CHAR8 *
TextDup (IN OABL_TEXT Text);

CHAR8 *
AsciiStrDup (IN CONST CHAR8 *String OPTIONAL);

// Growable string

// Zero it to start. A failed allocation sets Failed and drops further text,
// so a log line comes out short rather than not at all.
typedef struct _OABL_STRING {
  CHAR8  *Str;
  UINTN   Len, Cap;
  BOOLEAN Failed;
} OABL_STRING;

VOID
StringAppendN (
  IN OUT OABL_STRING *Builder,
  IN CONST CHAR8     *String,
  IN UINTN            Length
);

VOID
StringAppend (
  IN OUT OABL_STRING *Builder,
  IN CONST CHAR8     *String
);

VOID
StringAppendChar (
  IN OUT OABL_STRING *Builder,
  IN CHAR8            Char
);

STATIC inline VOID
StringAppendText (
  IN OUT OABL_STRING *Builder,
  IN OABL_TEXT        Text
)
{
  StringAppendN (Builder, Text.Ptr, Text.Len);
}

VOID
StringAppendCodePoint (
  IN OUT OABL_STRING *Builder,
  IN UINT32           CodePoint
);

VOID
StringPrint (
  IN OUT OABL_STRING *Builder,
  IN CONST CHAR8     *Format,
  ...
)
  __attribute__ ((format (
    printf,
    2,
    3
  )));

VOID
StringVPrint (
  IN OUT OABL_STRING *Builder,
  IN CONST CHAR8     *Format,
  IN va_list          Args
);

// Returns "" before anything is appended.
STATIC inline CONST CHAR8 *
StringGet (IN CONST OABL_STRING *Builder)
{
  return Builder->Str ? Builder->Str : "";
}

// Returns the text and empties the builder. Caller frees the result.
CHAR8 *
StringTake (IN OUT OABL_STRING *Builder);

VOID
StringFree (IN OUT OABL_STRING *Builder);

// Empties the builder but keeps its memory. Also clears Failed.
VOID
StringClear (IN OUT OABL_STRING *Builder);

// UTF-8 and UTF-16

// Decodes the code point at the start of Utf8 (Size > 0) and returns its
// byte count in Length. An invalid sequence decodes as U+FFFD, with the
// length of its longest valid prefix (at least 1), as Unicode recommends.
UINT32
Utf8Decode (
  IN CONST CHAR8 *Utf8,
  IN UINTN        Size,
  OUT UINTN      *Length
);

BOOLEAN
Utf8Valid (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
);

// Returns Bytes as UTF-8, with invalid sequences replaced by U+FFFD.
CHAR8 *
Utf8Lossy (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
);

// Returns Text as terminated UTF-16. Units excludes the terminator.
UINT16 *
Utf16FromText (
  IN OABL_TEXT Text,
  OUT UINTN   *Units OPTIONAL
);

STATIC inline UINT16 *
Utf16FromStr (IN CONST CHAR8 *String)
{
  return Utf16FromText (TextOf (String), NULL);
}

UINTN
Utf16Len (IN CONST UINT16 *String);

// Converts Count UTF-16 units. Unpaired surrogates become U+FFFD.
CHAR8 *
Utf16ToUtf8 (
  IN CONST UINT16 *Utf16,
  IN UINTN         Count
);

// Converts up to the terminator. NULL gives "".
STATIC inline CHAR8 *
Utf16ToStr (IN CONST UINT16 *String OPTIONAL)
{
  return Utf16ToUtf8 (String, String ? Utf16Len (String) : 0);
}

#endif
