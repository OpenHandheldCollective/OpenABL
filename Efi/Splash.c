/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Splash - keeping the firmware's splash on screen through the hand-over.
*/

#include "Efi/Splash.h"

#include "Base/Print.h"
#include "Base/Text.h"
#include "Base/Memory.h"
#include "Core/Panel.h"
#include "Efi/DisplayHandover.h"
#include "Efi/Handover.h"

STATIC BOOLEAN
DisplayOffAsked (VOID)
{
  return (DisplayHandoverQuiesceSetting () &
          (DISPLAY_HANDOVER_QUIESCE | DISPLAY_HANDOVER_LINES_OFF)) != 0;
}

// Reads the framebuffer of the graphics output on Handle, if a
// simple-framebuffer node can describe it.
STATIC BOOLEAN
FramebufferOf (
  IN EFI_HANDLE       Handle,
  OUT DT_FRAMEBUFFER *Out
)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop =
    GetProtocol (Handle, &gEfiGraphicsOutputProtocolGuid);
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Mode;
  CONST CHAR8                          *Format;

  if (!Gop) {
    return FALSE;
  }
  Mode = Gop->Mode->Info;
  // Little-endian pixels: XBL's B, G, R, X bytes are simplefb's a8r8g8b8.
  switch (Mode->PixelFormat) {
  case PixelBlueGreenRedReserved8BitPerColor:
    Format = "a8r8g8b8";
    break;
  case PixelRedGreenBlueReserved8BitPerColor:
    Format = "a8b8g8r8";
    break;
  default:
    return FALSE;
  }
  *Out = (DT_FRAMEBUFFER){
    .Base   = Gop->Mode->FrameBufferBase,
    .Size   = Gop->Mode->FrameBufferSize,
    .Width  = Mode->HorizontalResolution,
    .Height = Mode->VerticalResolution,
    .Stride = Mode->PixelsPerScanLine * 4,
    .Format = Format,
  };
  return TRUE;
}

BOOLEAN
SplashFramebuffer (OUT DT_FRAMEBUFFER *Out)
{
  EFI_HANDLE *Handles;
  UINTN       Count = LocateHandles (&gEfiGraphicsOutputProtocolGuid, &Handles);
  UINT64      RegionBase;
  UINT64      RegionLength;
  BOOLEAN     HaveRegion = HandoverDisplayRegion (&RegionBase, &RegionLength);
  BOOLEAN     Have       = FALSE;

  for (UINTN Index = 0; Index < Count; Index++) {
    DT_FRAMEBUFFER Fb;

    if (!FramebufferOf (Handles[Index], &Fb)) {
      continue;
    }
    if (HaveRegion && RegionBase <= Fb.Base &&
        Fb.Base + Fb.Size <= RegionBase + RegionLength) {
      *Out = Fb;
      Have = TRUE;
      break;
    }
    if (!Have) {
      *Out = Fb;
      Have = TRUE;
    }
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  return Have;
}

// Returns the memory to reserve for Fb: XBL's display region when it holds
// the framebuffer, as XBL keeps that from the OS, else the framebuffer's own
// pages.
STATIC VOID
RegionFor (
  IN CONST DT_FRAMEBUFFER *Fb,
  OUT UINT64              *Base,
  OUT UINT64              *Length
)
{
  UINT64 End = Fb->Base + Fb->Size;
  UINT64 RegionBase;
  UINT64 RegionLength;

  if (HandoverDisplayRegion (&RegionBase, &RegionLength) &&
      RegionBase <= Fb->Base && End <= RegionBase + RegionLength) {
    *Base   = RegionBase;
    *Length = RegionLength;
    return;
  }
  *Base   = Fb->Base & ~0xfffull;
  *Length = ((End + 0xfff) & ~0xfffull) - *Base;
}

VOID
SplashMarkDisplayOn (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN BOOLEAN    Declared,
  IN LOG_SINK   Log
)
{
  UINTN Chosen;

  (VOID)Declared;
  // A display-off request is the only way the display goes off: nothing
  // turns it off at ExitBootServices, even without a splash region.
  if (DisplayOffAsked ()) {
    return;
  }
  if (!DtNodeAtPath (Fdt, Cap, "/chosen", &Chosen)) {
    return;
  }
  if (DtSetPropAt (Fdt, Cap, Chosen, DISPLAY_HANDOVER_DISPLAY_ON_PROP, "", 0) ==
      DT_OK) {
    LogLine (
      Log, "splash: /chosen/openabl,display-on: the display is running at the "
           "hand-over"
    );
  } else {
    LogLine (Log, "splash: no room for /chosen/openabl,display-on");
  }
}

// Whether the tree already reserves the splash, as Android's and some
// Linux trees do.
STATIC BOOLEAN
HasSplashRegion (
  IN CONST UINT8 *Fdt,
  IN UINTN        Cap
)
{
  UINTN Node;

  return DtNodeAtPath (Fdt, Cap, "/reserved-memory/splash_region", &Node) ||
         DtNodeAtPath (Fdt, Cap, "/reserved-memory/cont_splash_region", &Node);
}

VOID
SplashReserveSplash (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN LOG_SINK   Log
)
{
  DT_FRAMEBUFFER Fb;
  CONST CHAR8   *What;
  UINT64         Base;
  UINT64         Length;
  BOOLEAN        Added;

  if (DisplayOffAsked ()) {
    return;
  }
  if (HasSplashRegion (Fdt, Cap)) {
    return;
  }
  if (!HandoverDisplayRegion (&Base, &Length)) {
    if (!SplashFramebuffer (&Fb)) {
      LogLine (
        Log, "splash: no splash region in the tree and no display memory known "
             "to reserve"
      );
      return;
    }
    RegionFor (&Fb, &Base, &Length);
  }
  if (DtReserveMemoryNamed (Fdt, Cap, "splash_region", Base, Length, &Added) !=
      DT_OK) {
    What = "NOT reserved (no room in the devicetree)";
  } else if (Added) {
    What = "reserved as /reserved-memory/splash_region";
  } else {
    What = "reserved already by the tree";
  }
  LogPrint (
    Log, "splash: XBL's display memory 0x%llx 0x%llx: %s", (UINT64)Base,
    (UINT64)Length, What
  );
}

// Whether a compatible string belongs to a mainline MDSS node.
STATIC BOOLEAN
IsMdss (
  IN CONST UINT8 *Compat,
  IN UINTN        Size,
  IN VOID        *Context
)
{
  STATIC CONST CHAR8 End[]  = "-mdss";
  CONST UINTN        EndLen = sizeof (End) - 1;

  (VOID)Context;
  return Size >= EndLen &&
         CompareMem (Compat + Size - EndLen, End, EndLen) == 0;
}

BOOLEAN
SplashDeclare (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN LOG_SINK   Log
)
{
  DT_FRAMEBUFFER_NODE Node;
  DT_FRAMEBUFFER      Fb;
  DT_ERROR            Error;
  UINT64              RegionBase;
  UINT64              RegionLength;
  BOOLEAN             Added;
  CHAR8               Reserved[64];

  if (!DisplayHandoverKeepLogoSetting ()) {
    LogLine (
      Log, "splash: not declared (Keep logo through boot is off): the stock "
           "hand-over"
    );
    return FALSE;
  }
  if (DisplayOffAsked ()) {
    LogLine (
      Log, "splash: not declared: DisplayQuiesce asks for the display off"
    );
    return FALSE;
  }
  // A downstream Android tree has Qualcomm's splash region and no mainline
  // MDSS node. The ABL's splash step handles it and the techpack driver
  // adopts the splash. Linux trees can have a splash_region too, but with
  // mainline display nodes, so they get the framebuffer node.
  if (HasSplashRegion (Fdt, Cap)) {
    DT_NODES Mdss = {0};
    BOOLEAN  None;

    DtNodesWhere (Fdt, Cap, IsMdss, NULL, &Mdss);
    None = Mdss.Count == 0;
    VecFree (&Mdss);
    if (None) {
      LogLine (
        Log,
        "splash: a downstream tree with Android's splash region: left to the "
        "kernel's own hand-over"
      );
      return FALSE;
    }
  }
  if (!SplashFramebuffer (&Fb)) {
    LogLine (Log, "splash: no linear framebuffer to declare");
    return FALSE;
  }
  RegionFor (&Fb, &RegionBase, &RegionLength);
  if (DtReserveMemory (Fdt, Cap, RegionBase, RegionLength, &Added) != DT_OK) {
    AsciiStrCopy (Reserved, sizeof (Reserved), ", NOT reserved (no room)");
  } else if (Added) {
    SPrint (
      Reserved, sizeof (Reserved), ", reserved 0x%llx 0x%llx",
      (UINT64)RegionBase, (UINT64)RegionLength
    );
  } else {
    AsciiStrCopy (Reserved, sizeof (Reserved), ", reserved already");
  }
  Error = DtAddFramebuffer (Fdt, Cap, &Fb, &Node);
  if (Error == DT_OK) {
    LogPrint (
      Log,
      "splash: /chosen/framebuffer@%llx: %ux%u %s, %llu display clocks%s%s; "
      "the display "
      "stays on",
      (UINT64)Fb.Base, (UINT32)Fb.Width, (UINT32)Fb.Height, Fb.Format,
      (UINT64)Node.Clocks,
      Node.PowerDomain ? ", the MDSS power domain" : ", no MDSS power domain",
      Reserved
    );
    return TRUE;
  }
  if (Error == DT_NO_SPACE) {
    LogLine (Log, "splash: no room in the devicetree for the framebuffer node");
  } else {
    LogLine (Log, "splash: not a devicetree to declare it in");
  }
  return FALSE;
}
