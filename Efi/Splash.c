/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Splash - hands XBL's splash over to the kernel's devicetree.
*/

#include "Efi/Splash.h"

#include "Base/Buffer.h"
#include "Efi/Handover.h"

// Defined in Efi.c; LinuxLoaderLib.h has only the macro.
extern EFI_GUID gQcomTokenSpaceGuid;

BOOLEAN
SplashFramebuffer (
  OUT UINT64 *Base,
  OUT UINT64 *Length
)
{
  EFI_HANDLE *Handles;
  UINTN   Count = LocateHandles (&gEfiGraphicsOutputProtocolGuid, &Handles);
  UINT64  Region       = 0;
  UINT64  RegionLength = 0;
  BOOLEAN HasRegion    = HandoverDisplayRegion (&Region, &RegionLength);
  BOOLEAN Found        = FALSE;

  // The panel's is the one in the display region, as DisplayOpenPanel
  // picks it.
  for (UINTN Pass = 0; Pass < 2 && !Found; Pass++) {
    for (UINTN Index = 0; Index < Count && !Found; Index++) {
      EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop =
        GetProtocol (Handles[Index], &gEfiGraphicsOutputProtocolGuid);
      UINT64 Start;
      UINT64 End;

      if (!Gop || !Gop->Mode || !Gop->Mode->FrameBufferSize) {
        continue;
      }
      Start = Gop->Mode->FrameBufferBase;
      End   = Start + Gop->Mode->FrameBufferSize;
      if (Pass == 0 && !(HasRegion && Region <= Start &&
                         Start < Region + RegionLength)) {
        continue;
      }
      *Base   = Start & ~0xfffull;
      *Length = ((End + 0xfff) & ~0xfffull) - *Base;
      Found   = TRUE;
    }
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  return Found;
}

// The splash region's node, by the names trees use.
STATIC CONST CHAR8 *CONST mSplashPaths[2] = {
  "/reserved-memory/splash_region",
  "/reserved-memory/cont_splash_region",
};

// The reg the stock ABL patches: <0 addr 0 size>, two cells each.
#define REG_LEN   16
#define TWO_CELLS 2

// Finds the splash region's node. Path gets its path, for the log.
STATIC BOOLEAN
SplashNode (
  IN CONST UINT8   *Fdt,
  IN UINTN          Cap,
  OUT UINTN        *Node,
  OUT CONST CHAR8 **Path
)
{
  for (UINTN Index = 0; Index < sizeof (mSplashPaths) / sizeof (*mSplashPaths);
       Index++) {
    if (DtNodeAtPath (Fdt, Cap, mSplashPaths[Index], Node)) {
      *Path = mSplashPaths[Index];
      return TRUE;
    }
  }
  return FALSE;
}

// XBL's framebuffer from DisplaySplashBufferInfo (version, address, size as
// UINT32s), else from the panel's graphics output; From names the source.
STATIC BOOLEAN
SplashBuffer (
  OUT UINT64       *Address,
  OUT UINT64       *Size,
  OUT CONST CHAR8 **From
)
{
  UINT8 Bytes[12];
  UINTN Length = sizeof (Bytes);

  if (gRT->GetVariable (
        L"DisplaySplashBufferInfo", &gQcomTokenSpaceGuid, NULL, &Length, Bytes
      ) == EFI_SUCCESS &&
      Length == sizeof (Bytes) && ReadLe32 (Bytes + 8) != 0) {
    *Address = ReadLe32 (Bytes + 4);
    *Size    = ReadLe32 (Bytes + 8);
    *From    = "DisplaySplashBufferInfo";
    return TRUE;
  }
  *From = "the graphics output";
  return SplashFramebuffer (Address, Size);
}

// A cells property of /reserved-memory, or Default without one.
STATIC UINT32
CellsOf (
  IN CONST UINT8 *Fdt,
  IN UINTN        Cap,
  IN CONST CHAR8 *Name,
  IN UINT32       Default
)
{
  UINTN      Node;
  OABL_BYTES Value;

  if (DtNodeAtPath (Fdt, Cap, "/reserved-memory", &Node) &&
      DtPropAt (Fdt, Cap, Node, Name, &Value) && Value.Len == 4) {
    return ReadBe32 (Value.Ptr);
  }
  return Default;
}

BOOLEAN
SplashHandOver (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN LOG_SINK   Log
)
{
  CONST CHAR8 *Path;
  CONST CHAR8 *From;
  UINTN        Node;
  OABL_BYTES   Reg;
  UINT8       *Cells;
  UINT64       Address;
  UINT64       Size;

  if (!SplashNode (Fdt, Cap, &Node, &Path)) {
    LogLine (
      Log, "splash: no splash region in the tree: the display is switched off "
           "before Linux"
    );
    return FALSE;
  }
  if (!DtPropAt (Fdt, Cap, Node, "reg", &Reg)) {
    LogPrint (
      Log, "splash: %s has no reg: left as it is; the display stays on", Path
    );
    return TRUE;
  }
  if (!SplashBuffer (&Address, &Size, &From)) {
    LogPrint (
      Log, "splash: %s kept as the tree has it (no framebuffer known); the "
           "display stays on",
      Path
    );
    return TRUE;
  }
  if (Reg.Len != REG_LEN ||
      CellsOf (Fdt, Cap, "#address-cells", TWO_CELLS) != TWO_CELLS ||
      CellsOf (Fdt, Cap, "#size-cells", TWO_CELLS) != TWO_CELLS) {
    LogPrint (
      Log, "splash: %s reg isn't <0 addr 0 size> (%llu bytes): left as it is; "
           "the display stays on",
      Path, (UINT64)Reg.Len
    );
    return TRUE;
  }
  // The value sits in the blob, which the caller owns.
  Cells = (UINT8 *)(UINTN)Reg.Ptr;
  WriteBe32 (Cells, (UINT32)(Address >> 32));
  WriteBe32 (Cells + 4, (UINT32)Address);
  WriteBe32 (Cells + 8, (UINT32)(Size >> 32));
  WriteBe32 (Cells + 12, (UINT32)Size);
  LogPrint (
    Log, "splash: %s reg -> XBL's framebuffer at 0x%llx, 0x%llx bytes (%s); "
         "the display stays on",
    Path, Address, Size, From
  );
  return TRUE;
}
