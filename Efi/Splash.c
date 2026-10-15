/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Splash - keeping the firmware's splash memory out of the kernel's hands.
*/

#include "Efi/Splash.h"

#include "Core/Panel.h"
#include "Efi/DisplayHandover.h"
#include "Efi/Handover.h"

STATIC BOOLEAN
DisplayOffAsked (VOID)
{
  return (DisplayHandoverQuiesceSetting () &
          (DISPLAY_HANDOVER_QUIESCE | DISPLAY_HANDOVER_LINES_OFF)) != 0;
}

// Returns the pages of the first graphics output's framebuffer.
STATIC BOOLEAN
FramebufferPages (
  OUT UINT64 *Base,
  OUT UINT64 *Length
)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop =
    LocateProtocol (&gEfiGraphicsOutputProtocolGuid);
  UINT64 End;

  if (!Gop || !Gop->Mode->FrameBufferSize) {
    return FALSE;
  }
  End     = Gop->Mode->FrameBufferBase + Gop->Mode->FrameBufferSize;
  *Base   = Gop->Mode->FrameBufferBase & ~0xfffull;
  *Length = ((End + 0xfff) & ~0xfffull) - *Base;
  return TRUE;
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
  CONST CHAR8 *What;
  UINT64       Base;
  UINT64       Length;
  BOOLEAN      Added;

  if (DisplayOffAsked ()) {
    return;
  }
  if (HasSplashRegion (Fdt, Cap)) {
    return;
  }
  if (!HandoverDisplayRegion (&Base, &Length) &&
      !FramebufferPages (&Base, &Length)) {
    LogLine (
      Log, "splash: no splash region in the tree and no display memory known "
           "to reserve"
    );
    return;
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
