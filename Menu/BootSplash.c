/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootSplash - draws over the firmware's logo as soon as the boot starts.

  With OABL_CARD_LOGO, a picture or animation from the card is shown if
  there is one. Otherwise the OpenABL wordmark is drawn.
*/

#include "Menu/Menu.h"

#include "Base/Memory.h"
#include "Base/Buffer.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/Bmp.h"
#include "Gfx/Gfx.h"
#include "Efi/Efi.h"
#include "Efi/Display.h"
#include "Efi/Files.h"
#include "Efi/Qcom.h"

extern CONST UINT8 gOablAssetFont[];
extern CONST UINTN gOablAssetFontSize;

// The panel, its rotation, and a canvas the size of the upright screen.
typedef struct _SPLASH_STATE {
  DISPLAY Display;
  UINT16  Degrees;
  CANVAS  Canvas;
  UINT32 *Px, *Scratch;
} SPLASH_STATE;

// Present's output callback has no context, so the display is kept here.
STATIC DISPLAY *mOut;

STATIC VOID
PresentOut (
  IN CONST UINT32 *Px,
  IN UINT32        Stride,
  IN UINT32        X,
  IN UINT32        Y,
  IN UINT32        W,
  IN UINT32        H
)
{
  DisplayBlit (mOut, Px, Stride, X, Y, W, H);
}

// Copies the canvas to the panel, rotated.
STATIC VOID
ShowFrame (IN OUT SPLASH_STATE *Splash)
{
  mOut = &Splash->Display;
  Present (&Splash->Canvas, Splash->Degrees, Splash->Scratch, PresentOut);
}

STATIC VOID
SplashFree (IN OUT SPLASH_STATE *Splash)
{
  FreePool (Splash->Px);
  FreePool (Splash->Scratch);
  Splash->Px = Splash->Scratch = NULL;
}

#ifdef OABL_CARD_LOGO
// A splash frame with 0x00RRGGBB pixels.
typedef struct _SPLASH_FRAME {
  UINTN   W, H;
  UINT32 *Px;
} SPLASH_FRAME;

// Draws Frame centred on the logical screen, over black.
STATIC VOID
DrawFrame (
  IN OUT CANVAS         *Canvas,
  IN CONST SPLASH_FRAME *Frame
)
{
  INT32 X0 = (Canvas->W - (INT32)Frame->W) / 2;
  INT32 Y0 = (Canvas->H - (INT32)Frame->H) / 2;

  FillRect (Canvas, 0, 0, Canvas->W, Canvas->H, 0x000000, 255);
  for (INT32 Y = 0; Y < (INT32)Frame->H; Y++) {
    INT32 Cy = Y0 + Y;
    if (Cy < 0 || Cy >= Canvas->H) {
      continue;
    }
    for (INT32 X = 0; X < (INT32)Frame->W; X++) {
      INT32 Cx = X0 + X;
      if (Cx >= 0 && Cx < Canvas->W) {
        Canvas->Pixels[(UINTN)Cy * (UINTN)Canvas->W + (UINTN)Cx] =
          Frame->Px[(UINTN)Y * Frame->W + (UINTN)X];
      }
    }
  }
}

// Orders two names (CONST CHAR8 *) by their bytes.
STATIC BOOLEAN
NameLess (
  IN CONST VOID *A,
  IN CONST VOID *B,
  IN VOID       *Context OPTIONAL
)
{
  CONST UINT8 *Left  = *(CONST UINT8 *CONST *)A;
  CONST UINT8 *Right = *(CONST UINT8 *CONST *)B;

  (VOID)Context;
  while (*Left && *Left == *Right) {
    Left++, Right++;
  }
  return *Left < *Right;
}

// Parses Text as a decimal UINT32. Fails on anything else, or on overflow.
STATIC BOOLEAN
ParseU32 (
  IN OABL_TEXT Text,
  OUT UINT32  *Value
)
{
  UINT64 Parsed;

  if (!TextParseU64 (Text, 10, &Parsed) || Parsed > MAX_UINT32) {
    return FALSE;
  }
  *Value = (UINT32)Parsed;
  return TRUE;
}

// Loads the BMPs (uncompressed, 24- or 32-bit) in \logo on the first
// allowed drive that has any. Several BMPs are animation frames in name
// order, played once at 10 a second unless \logo\fps says otherwise. They
// are drawn upright, so a landscape device takes a landscape picture.
STATIC BOOLEAN
CardSplash (
  IN CONST MENU_BOOT_CONTEXT *Ctx,
  OUT SPLASH_FRAME          **Frames,
  OUT UINTN                  *Count,
  OUT UINT32                 *Fps,
  OUT CHAR8                  *What,
  IN UINTN                    WhatSize
)
{
  enum { FRAME_LIMIT = 24 << 20, FRAMES = 64 };
  EFI_HANDLE *Volumes;
  UINTN       VolumeCount;
  BOOLEAN     Found = FALSE;

  MenuBootVolumes (Ctx, &Volumes, &VolumeCount);
  for (UINTN Index = 0; Index < VolumeCount && !Found; Index++) {
    FILES_DIR_ENTRIES Entries = {0};
    CONST CHAR8     **Names;
    UINTN             NameCount = 0;
    SPLASH_FRAME     *Loaded;
    UINTN             LoadedCount = 0;

    FilesList (Volumes[Index], "\\logo", &Entries);
    Names =
      AllocateZeroArray (Entries.Count ? Entries.Count : 1, sizeof (*Names));
    for (UINTN Index2 = 0; Names && Index2 < Entries.Count; Index2++) {
      OABL_TEXT Name = TextOf (Entries.Items[Index2].Name);
      if (!Entries.Items[Index2].IsDir && Name.Len > 4 &&
          TextEqNoCase (TextSub (Name, Name.Len - 4, Name.Len), ".bmp")) {
        Names[NameCount++] = Entries.Items[Index2].Name;
      }
    }
    if (!NameCount) {
      FreePool (Names);
      FilesEntriesFree (&Entries);
      continue;
    }
    SortBy (Names, NameCount, sizeof (*Names), NameLess, NULL);
    Loaded = AllocateZeroArray (
      NameCount < FRAMES ? NameCount : FRAMES, sizeof (*Loaded)
    );
    for (UINTN Index2 = 0; Loaded && Index2 < NameCount && Index2 < FRAMES;
         Index2++) {
      OABL_BUFFER File = {0};
      CHAR8       Path[300];

      AsciiStrCopy (Path, sizeof (Path), "\\logo\\");
      AsciiStrAppend (Path, sizeof (Path), Names[Index2]);
      if (FilesRead (Volumes[Index], Path, FRAME_LIMIT, &File) &&
          (Loaded[LoadedCount].Px = BmpDecode (
             File.Data, File.Len, &Loaded[LoadedCount].W, &Loaded[LoadedCount].H
           ))) {
        LoadedCount++;
      } else {
        MenuLog (
          "splash: %s is not a 24- or 32-bit uncompressed BMP, or too large; "
          "skipped",
          Path
        );
      }
      BufferFree (&File);
    }
    FreePool (Names);
    if (!LoadedCount) {
      FreePool (Loaded);
      FilesEntriesFree (&Entries);
      continue;
    }
    SPrint (
      What, WhatSize, "\\logo\\ (%llu%s", (UINT64)LoadedCount,
      LoadedCount == 1 ? " picture)" : " frames)"
    );
    {
      CHAR8 *Text = FilesReadText (Volumes[Index], "\\logo\\fps", 16);
      UINT32 Value;
      *Fps = Text && ParseU32 (TextTrim (TextOf (Text)), &Value) &&
                 Value >= 1 && Value <= 60
               ? Value
               : 10;
      FreePool (Text);
    }
    *Frames = Loaded;
    *Count  = LoadedCount;
    Found   = TRUE;
    FilesEntriesFree (&Entries);
  }
  FreePool (Volumes);
  return Found;
}

// The splash animation, run from a timer while the boot goes on. The
// callback interrupts the boot code, so AnimStop raises the TPL.
STATIC struct {
  BOOLEAN       On;
  SPLASH_FRAME *Frames;
  UINTN         Count, Next;
  SPLASH_STATE  Splash;
  EFI_EVENT     Event;
} mAnim;

STATIC VOID
AnimStop (VOID);

STATIC VOID
FramesFree (
  IN SPLASH_FRAME *Frames,
  IN UINTN         Count
)
{
  for (UINTN Index = 0; Index < Count; Index++) {
    FreePool (Frames[Index].Px);
  }
  FreePool (Frames);
}

// Draws the next frame. The frames play once and the last one stays, since
// the hand-over freezes whatever is on the panel.
STATIC VOID EFIAPI
AnimTick (
  IN EFI_EVENT Event,
  IN VOID     *Context OPTIONAL
)
{
  UINTN Index = mAnim.Next;

  (VOID)Event, (VOID)Context;
  if (!mAnim.On) {
    return;
  }
  if (Index == 1) {
    MenuLog ("splash: the animation runs (%llu ms)", (UINT64)ClockNowMs ());
  }
  DrawFrame (&mAnim.Splash.Canvas, &mAnim.Frames[Index]);
  ShowFrame (&mAnim.Splash);
  if (Index + 1 < mAnim.Count) {
    mAnim.Next = Index + 1;
  } else {
    MenuLog (
      "splash: the animation is through; its last frame stays (%llu ms)",
      (UINT64)ClockNowMs ()
    );
    AnimStop ();
  }
}

// Plays the frames after the first, which is already shown, at Fps. Takes
// over Frames and Splash's buffers, and frees them if it can't start.
STATIC VOID
AnimStart (
  IN SPLASH_FRAME       *Frames,
  IN UINTN               Count,
  IN UINT32              Fps,
  IN CONST SPLASH_STATE *Splash
)
{
  EFI_EVENT    Event;
  SPLASH_STATE Taken = *Splash;

  // The callback touches only mAnim and the graphics output.
  if (EFI_ERROR (gBS->CreateEvent (
        EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, AnimTick, NULL, &Event
      ))) {
    MenuLog ("splash: no timer for the animation");
    FramesFree (Frames, Count);
    SplashFree (&Taken);
    return;
  }
  // The period in 100 ns units.
  if (EFI_ERROR (
        gBS->SetTimer (Event, TimerPeriodic, 1000000 / (Fps ? Fps : 1) * 10)
      )) {
    MenuLog ("splash: the animation's timer didn't start");
    gBS->CloseEvent (Event);
    FramesFree (Frames, Count);
    SplashFree (&Taken);
    return;
  }
  // Set up after SetTimer: the callback first runs one period later.
  mAnim.Frames = Frames;
  mAnim.Count  = Count;
  mAnim.Next   = 1;
  mAnim.Splash = *Splash;
  mAnim.Event  = Event;
  mAnim.On     = TRUE;
}

// Stops the animation and frees it. The last frame stays on the panel.
STATIC VOID
AnimStop (VOID)
{
  // Keep the callback out while tearing down. When called from the callback
  // itself, the raise does nothing.
  EFI_TPL Old = gBS->RaiseTPL (TPL_CALLBACK);

  if (mAnim.On) {
    mAnim.On = FALSE;
    gBS->CloseEvent (mAnim.Event);
    FramesFree (mAnim.Frames, mAnim.Count);
    SplashFree (&mAnim.Splash);
    mAnim.Frames = NULL;
    mAnim.Count  = 0;
  }
  gBS->RestoreTPL (Old);
}

VOID
MenuAnimFinish (VOID)
{
  // Keep the tick from running in between.
  EFI_TPL Old = gBS->RaiseTPL (TPL_CALLBACK);

  if (mAnim.On && mAnim.Count && mAnim.Next < mAnim.Count) {
    DrawFrame (&mAnim.Splash.Canvas, &mAnim.Frames[mAnim.Count - 1]);
    ShowFrame (&mAnim.Splash);
    MenuLog (
      "splash: the animation finished early, as the boot goes on; its last "
      "frame stays"
    );
  }
  gBS->RestoreTPL (Old);
  AnimStop ();
}
#endif

VOID
MenuBootSplash (
  IN CONST MENU_HOST         *Host,
  IN CONST MENU_BOOT_CONTEXT *Ctx
)
{
  SPLASH_STATE Splash = {0};
  EFI_STATUS   Status = DisplayOpenPanel (&Splash.Display);
  UINTN        PanelW;
  UINTN        PanelH;
  UINT32       LogicalW;
  UINT32       LogicalH;
  UINT16       PanelRotation;
  UINT16       FirmwareRotation = 0;
  CHAR8        Size[64];

  (VOID)Host;
  if (EFI_ERROR (Status)) {
    MenuLog ("splash: no graphics output: %s", MENU_STATUS (Status));
    return;
  }
  DisplaySize (&Splash.Display, &PanelW, &PanelH);
  // Rotate as the menu would: the model's rotation or the tall panel
  // default, plus the firmware's own rotation.
  if (Ctx->ModelRotation == ROTATION_NONE && PanelH > PanelW) {
    PanelRotation = 270;
  } else if (Ctx->Rotation == ROTATION_AUTO) {
    PanelRotation =
      (Ctx->ModelRotation == ROTATION_NONE ? 0 : Ctx->ModelRotation) % 360;
  } else {
    PanelRotation = Ctx->Rotation % 360;
  }
  QcomFirmwarePanelRotation (PanelW, PanelH, &FirmwareRotation);
  Splash.Degrees = (UINT16)((PanelRotation + FirmwareRotation) % 360);
  LogicalSize (
    (UINT32)PanelW, (UINT32)PanelH, Splash.Degrees, &LogicalW, &LogicalH
  );
  Splash.Px      = AllocatePool ((UINTN)LogicalW * LogicalH * 4);
  Splash.Scratch = AllocatePool ((UINTN)PanelW * PanelH * 4);
  if (!Splash.Px || !Splash.Scratch) {
    SplashFree (&Splash);
    return;
  }
  CanvasInit (&Splash.Canvas, Splash.Px, (INT32)LogicalW, (INT32)LogicalH);
  SPrint (
    Size, sizeof (Size), "%llux%llu, turned %u\xc2\xb0", (UINT64)PanelW,
    (UINT64)PanelH, Splash.Degrees
  );
#ifdef OABL_CARD_LOGO
  {
    SPLASH_FRAME *Frames;
    UINTN         Count;
    UINT32        Fps;
    CHAR8         What[48];

    if (CardSplash (Ctx, &Frames, &Count, &Fps, What, sizeof (What))) {
      BOOLEAN Animated = Count > 1;
      CHAR8   Rate[12];

      DrawFrame (&Splash.Canvas, &Frames[0]);
      ShowFrame (&Splash);
      AsciiStrFromU64 (Rate, sizeof (Rate), Fps);
      MenuLog (
        "splash: the card's %s (%llux%llu) over the firmware's logo on the "
        "panel (%s%s%s%s",
        What, (UINT64)Frames[0].W, (UINT64)Frames[0].H, Size,
        Animated ? "), animated at " : ")", Animated ? Rate : "",
        Animated ? " frames a second" : ""
      );
      if (Animated) {
        AnimStart (Frames, Count, Fps, &Splash);
      } else {
        FramesFree (Frames, Count);
        SplashFree (&Splash);
      }
      return;
    }
  }
#endif
  FillRect (
    &Splash.Canvas, 0, 0, Splash.Canvas.W, Splash.Canvas.H, 0x000000, 255
  );
  if (FontLoad (gOablAssetFont, gOablAssetFontSize)) {
    // Capitals 8.5% of the screen tall, at least 24 pixels, and no wider
    // than 60% of the screen.
    float CapH = (float)LogicalH * 0.085f;
    float Em;
    float W;
    float OpenW;
    float X;
    if (CapH < 24.0f) {
      CapH = 24.0f;
    }
    Em = CapH / FontCapHeight (1.0f);
    W  = TextAdvance (Em, FONT_WEIGHT_MEDIUM, "OpenABL");
    if (W > (float)LogicalW * 0.6f) {
      Em *= (float)LogicalW * 0.6f / W;
      CapH = FontCapHeight (Em);
      W    = TextAdvance (Em, FONT_WEIGHT_MEDIUM, "OpenABL");
    }
    OpenW = TextAdvance (Em, FONT_WEIGHT_MEDIUM, "Open");
    X     = ((float)LogicalW - W) / 2.0f;
    DrawText (
      &Splash.Canvas, X, ((float)LogicalH + CapH) / 2.0f, Em,
      FONT_WEIGHT_MEDIUM, "Open", 0xFFFFFF
    );
    DrawText (
      &Splash.Canvas, X + OpenW, ((float)LogicalH + CapH) / 2.0f, Em,
      FONT_WEIGHT_MEDIUM, "ABL", 0xFF5555
    );
  }
  ShowFrame (&Splash);
  MenuLog (
    "splash: the OpenABL wordmark drawn over the firmware's on the panel (%s)",
    Size
  );
  SplashFree (&Splash);
}
