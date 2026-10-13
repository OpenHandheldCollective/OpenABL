/* SPDX-License-Identifier: GPL-3.0-or-later AND BSD-2-Clause */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * 5x12 font from Android, Copyright (C) 2008 The Android Open Source
 * Project. All rights reserved.
 */

/**
  Abl - the ABL's log console and library protocol.
*/

#include "Efi/Abl.h"

#include <Protocol/OpenAblAblLib.h>

#include "Core/LibFdt.h"

// Log console

#define LOG_SIZE (32 * 1024)
#define FONT_W   5
#define FONT_H   12
#define SCALE    2
#define FG       0xffffffffu
#define BG       0xff000000u

// 5x12 glyphs for characters 32 to 127 (Android's font5x12.h). Two words per
// glyph, six rows of five bits per word, leftmost pixel in the lowest bit.
STATIC CONST UINT32 mFont[192] = {
  0x00000000, 0x00000000, 0x08421080, 0x00020084, 0x00052940, 0x00000000,
  0x15f52800, 0x0000295f, 0x1c52f880, 0x00023e94, 0x08855640, 0x0004d542,
  0x04528800, 0x000b2725, 0x00021080, 0x00000000, 0x04211088, 0x00821042,
  0x10841082, 0x00221108, 0x09575480, 0x00000000, 0x3e420000, 0x00000084,
  0x00000000, 0x00223000, 0x3e000000, 0x00000000, 0x00000000, 0x00471000,
  0x08844200, 0x00008442, 0x2318a880, 0x00022a31, 0x08429880, 0x000f9084,
  0x1108c5c0, 0x000f8444, 0x1c4443e0, 0x00074610, 0x14a62100, 0x000423e9,
  0x26d087e0, 0x00074610, 0x1e10c5c0, 0x00074631, 0x088443e0, 0x00010844,
  0x1d18c5c0, 0x00074631, 0x3d18c5c0, 0x00074610, 0x08e20000, 0x00471000,
  0x08e20000, 0x00223000, 0x02222200, 0x00082082, 0x01f00000, 0x000003e0,
  0x20820820, 0x00008888, 0x1108c5c0, 0x00020084, 0x2b98c5c0, 0x000f05b5,
  0x2318a880, 0x0008c63f, 0x1d2949e0, 0x0007ca52, 0x0210c5c0, 0x00074421,
  0x252949e0, 0x0007ca52, 0x1e1087e0, 0x000f8421, 0x1e1087e0, 0x00008421,
  0x0210c5c0, 0x00074639, 0x3f18c620, 0x0008c631, 0x084211c0, 0x00071084,
  0x10842380, 0x00032508, 0x0654c620, 0x0008c525, 0x02108420, 0x000f8421,
  0x2b5dc620, 0x0008c631, 0x2b59ce20, 0x0008c739, 0x2318c5c0, 0x00074631,
  0x1f18c5e0, 0x00008421, 0x2318c5c0, 0x01075631, 0x1f18c5e0, 0x0008c525,
  0x1c10c5c0, 0x00074610, 0x084213e0, 0x00021084, 0x2318c620, 0x00074631,
  0x1518c620, 0x0002114a, 0x2b18c620, 0x000556b5, 0x08a54620, 0x0008c54a,
  0x08a54620, 0x00021084, 0x088443e0, 0x000f8442, 0x0421084e, 0x00e10842,
  0x08210420, 0x00084108, 0x1084210e, 0x00e42108, 0x0008a880, 0x00000000,
  0x00000000, 0x01f00000, 0x00000104, 0x00000000, 0x20e00000, 0x000b663e,
  0x22f08420, 0x0007c631, 0x22e00000, 0x00074421, 0x23e84200, 0x000f4631,
  0x22e00000, 0x0007443f, 0x1e214980, 0x00010842, 0x22e00000, 0x1d187a31,
  0x26d08420, 0x0008c631, 0x08601000, 0x00071084, 0x10c02000, 0x0c94a108,
  0x0a908420, 0x0008a4a3, 0x084210c0, 0x00071084, 0x2ab00000, 0x0008d6b5,
  0x26d00000, 0x0008c631, 0x22e00000, 0x00074631, 0x22f00000, 0x0210be31,
  0x23e00000, 0x21087a31, 0x26d00000, 0x00008421, 0x22e00000, 0x00074506,
  0x04f10800, 0x00064842, 0x23100000, 0x000b6631, 0x23100000, 0x00022951,
  0x23100000, 0x000556b5, 0x15100000, 0x0008a884, 0x23100000, 0x1d185b31,
  0x11f00000, 0x000f8444, 0x06421098, 0x01821084, 0x08421080, 0x00021084,
  0x30421083, 0x00321084, 0x0004d640, 0x00000000, 0x00000000, 0x00000000,
};

// Single CPU. The sink only runs inside a DEBUG call, never in the middle of
// one of our own console calls, so the two don't overlap.
typedef struct _CONSOLE {
  UINT8 Log[LOG_SIZE];
  // Total bytes written. The next one goes at End % LOG_SIZE.
  UINTN  End;
  UINT8 *Fb;
  // Pixels per scan line, and number of lines.
  UINTN Width;
  UINTN Height;
  UINTN X;
  UINTN Y;
  UINTN MaxX;
  UINTN MaxY;
} CONSOLE;

STATIC CONSOLE mConsole;
STATIC BOOLEAN mDrawn;
STATIC BOOLEAN mHeld;
STATIC BOOLEAN mDirect;

STATIC VOID
ConWrite (
  IN OUT CONSOLE *Console,
  IN CONST UINT8 *Bytes,
  IN UINTN        Count
);

VOID
AblConsoleDirect (IN BOOLEAN On)
{
  mDirect = On;
}

VOID
AblConsoleProgramLine (IN CONST CHAR8 *Line)
{
  if (mDirect) {
    UINTN Length = 0;

    while (Line[Length]) {
      Length++;
    }
    ConWrite (&mConsole, (CONST UINT8 *)Line, Length);
    ConWrite (&mConsole, (CONST UINT8 *)"\r\n", 2);
  }
}

VOID EFIAPI
AblConsoleSink (
  IN CONST UINT8 *Buffer OPTIONAL,
  IN UINTN        Length
)
{
  if (!Buffer) {
    return;
  }
  ConWrite (&mConsole, Buffer, Length);
}

VOID
AblConsoleShow (VOID)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL         *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN                                 Width;
  UINTN                                 Height;

  if (!gST) {
    return;
  }
  Gop = GetProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid);
  if (!Gop) {
    return;
  }
  Info            = Gop->Mode->Info;
  Width           = Info->PixelsPerScanLine != 0 ? Info->PixelsPerScanLine
                                                 : Info->HorizontalResolution;
  Height          = Info->VerticalResolution;
  mConsole.Fb     = (UINT8 *)(UINTN)Gop->Mode->FrameBufferBase;
  mConsole.Width  = Width;
  mConsole.Height = Height;
  mConsole.X      = 0;
  mConsole.Y      = 0;
  mConsole.MaxX   = Width / (FONT_W + 1);
  mConsole.MaxY   = (Height - 1) / FONT_H;
  mDrawn          = TRUE;
}

VOID
AblConsoleHide (VOID)
{
  mDrawn = FALSE;
}

VOID
AblConsoleHold (IN BOOLEAN On)
{
  mHeld = On;
}

BOOLEAN
AblConsoleRead (
  IN UINTN         Max,
  OUT OABL_BUFFER *Out
)
{
  CONST CONSOLE *Console = &mConsole;
  UINTN          Count   = Console->End;

  if (Count > LOG_SIZE) {
    Count = LOG_SIZE;
  }
  if (Count > Max) {
    Count = Max;
  }
  *Out = (OABL_BUFFER){0};
  if (!BufferReserve (Out, Count)) {
    return FALSE;
  }
  for (UINTN Index = Console->End - Count; Index < Console->End; Index++) {
    Out->Data[Out->Len++] = Console->Log[Index % LOG_SIZE];
  }
  return TRUE;
}

// Masks IRQs and returns the previous DAIF, as the ABL's console did around
// a write, so a timer callback that logs can't cut into another line. Uses
// no boot service, so it works after ExitBootServices too.
STATIC UINT64
IrqOff (VOID)
{
  UINT64 Daif;

  __asm__ volatile ("mrs %0, daif\n\tmsr daifset, #2"
                    : "=r"(Daif)
                    :
                    : "memory");
  return Daif;
}

STATIC VOID
IrqRestore (IN UINT64 Daif)
{
  __asm__ volatile ("msr daif, %0" : : "r"(Daif) : "memory");
}

STATIC VOID
Pixel (
  IN CONST CONSOLE *Console,
  IN UINTN          X,
  IN UINTN          Y,
  IN UINT32         Color
)
{
  if (X < Console->Width && Y < Console->Height) {
    *(volatile UINT32 *)(Console->Fb + (Y * Console->Width + X) * 4) = Color;
  }
}

// Draws Char at the cursor.
STATIC VOID
Glyph (
  IN CONST CONSOLE *Console,
  IN UINT8          Char
)
{
  UINTN X0    = Console->X * SCALE * (FONT_W + 1);
  UINTN Y0    = Console->Y * FONT_H;
  UINTN First = (UINTN)(Char - 32) * 2;

  for (UINTN Row = 0; Row < FONT_H; Row++) {
    UINT32 Bits = mFont[First + Row / 6] >> ((Row % 6) * FONT_W);

    for (UINTN Col = 0; Col < FONT_W; Col++) {
      UINT32 Color = Bits >> Col & 1 ? FG : BG;

      for (UINTN Dy = 0; Dy < SCALE; Dy++) {
        for (UINTN Dx = 0; Dx < SCALE; Dx++) {
          Pixel (Console, X0 + Col * SCALE + Dx, Y0 + Row * SCALE + Dy, Color);
        }
      }
    }
  }
}

// Cleans pixel lines From to To to the point of coherency so the display
// engine sees them.
STATIC VOID
Flush (
  IN CONST CONSOLE *Console,
  IN UINTN          From,
  IN UINTN          To
)
{
  UINTN Start = (UINTN)Console->Fb + From * Console->Width * 4;
  UINTN End =
    (UINTN)Console->Fb +
    (To < Console->Height ? To : Console->Height) * Console->Width * 4;

  for (UINTN Address = Start & ~(UINTN)63; Address < End; Address += 64) {
    __asm__ volatile ("dc civac, %0" : : "r"(Address) : "memory");
  }
  __asm__ volatile ("dsb sy" : : : "memory");
}

// Draws one character the way the ABL's FbConPutCharWithFactor does. A wrap
// goes back to the top and draws the character again there.
STATIC VOID
Put (
  IN OUT CONSOLE *Console,
  IN UINT8        Char
)
{
  if (Char > 127) {
    return;
  }
  for (;;) {
    UINTN Top;

    if (Char < 32) {
      if (Char == '\r') {
        Console->X = 0;
        return;
      }
      if (Char != '\n') {
        return;
      }
    } else {
      if (Console->X == 0 && Char == ' ') {
        return;
      }
      Glyph (Console, Char);
      Console->X++;
      if (Console->X < Console->MaxX / SCALE) {
        return;
      }
    }
    Console->Y += SCALE;
    Console->X = 0;
    if (Console->Y >= (Console->MaxY > SCALE ? Console->MaxY - SCALE : 0)) {
      Flush (Console, 0, Console->Height);
      Console->Y = 0;
      continue;
    }
    Top = Console->Y * FONT_H;
    for (UINTN Y = Top; Y < Top + FONT_H * SCALE && Y < Console->Height; Y++) {
      for (UINTN X = 0; X < Console->Width; X++) {
        Pixel (Console, X, Y, 0);
      }
    }
    Flush (Console, 0, Console->Height);
    return;
  }
}

// Stores Count bytes in the ring, and draws them if the log is shown.
STATIC VOID
ConWrite (
  IN OUT CONSOLE *Console,
  IN CONST UINT8 *Bytes,
  IN UINTN        Count
)
{
  UINT64 Daif = IrqOff ();

  for (UINTN Index = 0; Index < Count; Index++) {
    Console->Log[Console->End % LOG_SIZE] = Bytes[Index];
    Console->End++;
  }
  if (mDrawn && !mHeld && Console->Fb) {
    for (UINTN Index = 0; Index < Count; Index++) {
      Put (Console, Bytes[Index]);
    }
  }
  IrqRestore (Daif);
}

// ABL library protocol

STATIC EFI_GUID mAblLibGuid = OPENABL_ABL_LIB_PROTOCOL_GUID;

STATIC OPENABL_ABL_LIB_PROTOCOL *mLib;

// What the libfdt wrappers return without the protocol.
#define NO_LIB (-FDT_ERR_BADSTATE)

BOOLEAN
AblLibInit (VOID)
{
  mLib = LocateProtocol (&mAblLibGuid);
  return mLib != NULL;
}

EFI_STATUS
AblLibInflate (
  IN CONST VOID *In,
  IN UINTN       InSize,
  OUT VOID      *Out,
  IN UINTN       OutSize,
  OUT UINTN     *Written,
  OUT UINTN     *Used
)
{
  UINTN      Produced = 0;
  UINTN      Consumed = 0;
  EFI_STATUS Status;

  if (!mLib) {
    return EFI_UNSUPPORTED;
  }
  Status = mLib->Inflate (mLib, In, InSize, Out, OutSize, &Produced, &Consumed);
  *Written = Produced;
  *Used    = Consumed;
  return Status;
}

// libfdt, forwarded to the ABL.

INT32
fdt_path_offset (
  IN CONST VOID  *Fdt,
  IN CONST CHAR8 *Path
)
{
  return mLib ? mLib->fdt_path_offset (Fdt, Path) : NO_LIB;
}

CONST CHAR8 *
fdt_get_name (
  IN CONST VOID *Fdt,
  IN INT32       Nodeoffset,
  OUT INT32     *Lenp OPTIONAL
)
{
  if (mLib) {
    return mLib->fdt_get_name (Fdt, Nodeoffset, Lenp);
  }
  if (Lenp) {
    *Lenp = NO_LIB;
  }
  return NULL;
}

CONST VOID *
fdt_getprop (
  IN CONST VOID  *Fdt,
  IN INT32        Nodeoffset,
  IN CONST CHAR8 *Name,
  OUT INT32      *Lenp OPTIONAL
)
{
  if (mLib) {
    return mLib->fdt_getprop (Fdt, Nodeoffset, Name, Lenp);
  }
  if (Lenp) {
    *Lenp = NO_LIB;
  }
  return NULL;
}

INT32
fdt_next_node (
  IN CONST VOID *Fdt,
  IN INT32       Offset,
  IN OUT INT32  *Depth OPTIONAL
)
{
  return mLib ? mLib->fdt_next_node (Fdt, Offset, Depth) : NO_LIB;
}

INT32
fdt_first_subnode (
  IN CONST VOID *Fdt,
  IN INT32       Offset
)
{
  return mLib ? mLib->fdt_first_subnode (Fdt, Offset) : NO_LIB;
}

INT32
fdt_next_subnode (
  IN CONST VOID *Fdt,
  IN INT32       Offset
)
{
  return mLib ? mLib->fdt_next_subnode (Fdt, Offset) : NO_LIB;
}

INT32
fdt_node_offset_by_compatible (
  IN CONST VOID  *Fdt,
  IN INT32        Startoffset,
  IN CONST CHAR8 *Compatible
)
{
  return mLib
           ? mLib->fdt_node_offset_by_compatible (Fdt, Startoffset, Compatible)
           : NO_LIB;
}

INT32
fdt_setprop (
  IN OUT VOID    *Fdt,
  IN INT32        Nodeoffset,
  IN CONST CHAR8 *Name,
  IN CONST VOID  *Val,
  IN INT32        Len
)
{
  return mLib ? mLib->fdt_setprop (Fdt, Nodeoffset, Name, Val, Len) : NO_LIB;
}

INT32
fdt_add_subnode (
  IN OUT VOID    *Fdt,
  IN INT32        Parentoffset,
  IN CONST CHAR8 *Name
)
{
  return mLib ? mLib->fdt_add_subnode (Fdt, Parentoffset, Name) : NO_LIB;
}
