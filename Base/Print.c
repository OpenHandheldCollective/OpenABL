/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Print - printf-style formatting without a C library.
*/

#include "Base/Print.h"
#include "Base/Memory.h"

#include "Base/OablBase.h"

// Count includes the characters that didn't fit in Buffer.
typedef struct _PRINT_OUT {
  CHAR8 *Buffer;
  UINTN  Size, Count;
} PRINT_OUT;

STATIC VOID
Put (
  IN OUT PRINT_OUT *Out,
  IN CHAR8          Char
)
{
  if (Out->Count + 1 < Out->Size) {
    Out->Buffer[Out->Count] = Char;
  }
  Out->Count++;
}

STATIC VOID
Pad (
  IN OUT PRINT_OUT *Out,
  IN CHAR8          Char,
  IN INT32          Count
)
{
  while (Count-- > 0) {
    Put (Out, Char);
  }
}

INT32
VSPrint (
  OUT CHAR8      *Buffer OPTIONAL,
  IN UINTN        BufferSize,
  IN CONST CHAR8 *Format,
  IN va_list      Args
)
{
  PRINT_OUT Out = {Buffer, BufferSize, 0};

  while (*Format) {
    BOOLEAN Left      = FALSE;
    BOOLEAN Zero      = FALSE;
    BOOLEAN Plus      = FALSE;
    BOOLEAN Space     = FALSE;
    BOOLEAN Alt       = FALSE;
    INT32   Width     = 0;
    INT32   Precision = -1;
    // Length modifier: -2 for hh, -1 h, 1 l, 2 ll, z, j or t.
    INT32        Length = 0;
    CHAR8        Digits[24];
    CHAR8        Sign = 0;
    CHAR8        Conversion;
    CONST CHAR8 *Text = NULL, *Prefix = "";
    INT32        TextLength = 0;
    INT32        Zeros      = 0;

    if (*Format != '%') {
      Put (&Out, *Format++);
      continue;
    }
    Format++;
    for (;; Format++) {
      if (*Format == '-') {
        Left = TRUE;
      } else if (*Format == '0') {
        Zero = TRUE;
      } else if (*Format == '+') {
        Plus = TRUE;
      } else if (*Format == ' ') {
        Space = TRUE;
      } else if (*Format == '#') {
        Alt = TRUE;
      } else {
        break;
      }
    }
    if (*Format == '*') {
      Width = va_arg (Args, INT32);
      if (Width < 0) {
        Left  = TRUE;
        Width = -Width;
      }
      Format++;
    } else {
      while (*Format >= '0' && *Format <= '9') {
        Width = Width * 10 + (*Format++ - '0');
      }
    }
    if (*Format == '.') {
      Format++;
      Precision = 0;
      if (*Format == '*') {
        Precision = va_arg (Args, INT32);
        Format++;
      } else {
        while (*Format >= '0' && *Format <= '9') {
          Precision = Precision * 10 + (*Format++ - '0');
        }
      }
    }
    for (;; Format++) {
      if (*Format == 'h') {
        Length = Length < 0 ? -2 : -1;
      } else if (*Format == 'l') {
        Length = Length > 0 ? 2 : 1;
      } else if (*Format == 'z' || *Format == 'j' || *Format == 't') {
        Length = 2;
      } else {
        break;
      }
    }
    Conversion = *Format;
    if (!Conversion) {
      break;
    }
    Format++;
    switch (Conversion) {
    case '%':
      Put (&Out, '%');
      continue;
    case 'c':
      Digits[0]  = (CHAR8)va_arg (Args, INT32);
      Text       = Digits;
      TextLength = 1;
      Zero       = FALSE;
      break;
    case 's':
      Text = va_arg (Args, CONST CHAR8 *);
      if (!Text) {
        Text = "(null)";
      }
      while ((Precision < 0 || TextLength < Precision) && Text[TextLength]) {
        TextLength++;
      }
      Zero = FALSE;
      break;
    case 'd':
    case 'i':
    case 'u':
    case 'x':
    case 'X':
    case 'o':
    case 'p': {
      UINT64 Value;
      UINT32 Base =
        Conversion == 'o'                                               ? 8
        : (Conversion == 'x' || Conversion == 'X' || Conversion == 'p') ? 16
                                                                        : 10;
      CONST CHAR8 *DigitChars =
        Conversion == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
      INT32 At = (INT32)sizeof (Digits);

      if (Conversion == 'p') {
        Value = (UINTN)va_arg (Args, VOID *);
        Alt   = TRUE;
      } else if (Conversion == 'd' || Conversion == 'i') {
        INT64 Signed = Length >= 2   ? va_arg (Args, INT64)
                       : Length == 1 ? va_arg (Args, long)
                                     : va_arg (Args, INT32);
        if (Length == -1) {
          Signed = (INT16)Signed;
        } else if (Length == -2) {
          Signed = (INT8)Signed;
        }
        if (Signed < 0) {
          Sign  = '-';
          Value = (UINT64)0 - (UINT64)Signed;
        } else {
          Sign  = Plus ? '+' : Space ? ' ' : 0;
          Value = (UINT64)Signed;
        }
      } else {
        Value = Length >= 2   ? va_arg (Args, UINT64)
                : Length == 1 ? va_arg (Args, unsigned long)
                              : va_arg (Args, UINT32);
        if (Length == -1) {
          Value = (UINT16)Value;
        } else if (Length == -2) {
          Value = (UINT8)Value;
        }
      }
      // C prints no 0x prefix for zero.
      if (!Value && Conversion != 'p' && Conversion != 'o') {
        Alt = FALSE;
      }
      while (Value || (At == (INT32)sizeof (Digits) && Precision != 0)) {
        Digits[--At] = DigitChars[Value % Base];
        Value /= Base;
      }
      Text       = Digits + At;
      TextLength = (INT32)sizeof (Digits) - At;
      // Precision zeros are padded separately. Digits only has room for
      // the number itself.
      if (Precision > TextLength) {
        Zeros = Precision - TextLength;
      }
      if (Alt) {
        Prefix = Conversion == 'o' ? "0" : Conversion == 'X' ? "0X" : "0x";
      }
      if (Alt && Conversion == 'o' &&
          (Zeros || (TextLength && Digits[At] == '0'))) {
        Prefix = "";
      }
      if (Precision >= 0) {
        Zero = FALSE;
      }
      break;
    }
    default:
      // Print an unknown conversion as it is.
      Put (&Out, '%');
      Put (&Out, Conversion);
      continue;
    }
    {
      INT32 PrefixLength = 0;
      INT32 Total;
      while (Prefix[PrefixLength]) {
        PrefixLength++;
      }
      Total = TextLength + Zeros + PrefixLength + (Sign ? 1 : 0);
      if (!Left && !Zero) {
        Pad (&Out, ' ', Width - Total);
      }
      if (Sign) {
        Put (&Out, Sign);
      }
      for (INT32 Index = 0; Index < PrefixLength; Index++) {
        Put (&Out, Prefix[Index]);
      }
      if (!Left && Zero) {
        Pad (&Out, '0', Width - Total);
      }
      Pad (&Out, '0', Zeros);
      for (INT32 Index = 0; Index < TextLength; Index++) {
        Put (&Out, Text[Index]);
      }
      if (Left) {
        Pad (&Out, ' ', Width - Total);
      }
    }
  }
  if (BufferSize) {
    Out.Buffer[Out.Count < BufferSize ? Out.Count : BufferSize - 1] = 0;
  }
  return (INT32)Out.Count;
}

INT32
SPrint (
  OUT CHAR8      *Buffer OPTIONAL,
  IN UINTN        BufferSize,
  IN CONST CHAR8 *Format,
  ...
)
{
  va_list Args;
  INT32   Length;
  va_start (Args, Format);
  Length = VSPrint (Buffer, BufferSize, Format, Args);
  va_end (Args);
  return Length;
}

CHAR8 *
VSPrintAlloc (
  IN CONST CHAR8 *Format,
  IN va_list      Args
)
{
  va_list Again;
  CHAR8  *Text;
  INT32   Length;

  va_copy (Again, Args);
  Length = VSPrint (NULL, 0, Format, Args);
  Text   = Length >= 0 ? AllocatePool ((UINTN)Length + 1) : NULL;
  if (Text) {
    VSPrint (Text, (UINTN)Length + 1, Format, Again);
  }
  va_end (Again);
  return Text;
}

CHAR8 *
SPrintAlloc (
  IN CONST CHAR8 *Format,
  ...
)
{
  va_list Args;
  CHAR8  *Text;
  va_start (Args, Format);
  Text = VSPrintAlloc (Format, Args);
  va_end (Args);
  return Text;
}
