/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DtFixup - EFI_DT_FIXUP_PROTOCOL, for OS loaders that bring their own
  devicetree.
*/

#include "Efi/DtFixup.h"

#include <Guid/Fdt.h>

#include "Base/Buffer.h"
#include "Efi/DisplayHandover.h"
#include "Efi/Splash.h"

// U-Boot's EFI_DT_FIXUP_PROTOCOL: its GUID, revision and flags.
STATIC EFI_GUID mDtFixupGuid = {
  0xe617d64c,
  0xfe08,
  0x46da,
  {0xf4, 0xdc, 0xbb, 0xd5, 0x87, 0x0c, 0x73, 0x00}};
#define DT_FIXUP_REVISION 0x00010000ull
#define DT_APPLY_FIXUPS   1u
#define DT_RESERVE_MEMORY 2u
#define DT_INSTALL_TABLE  4u

#define FDT_MAGIC       0xd00dfeedu
#define FDT_HEADER_SIZE 40u

typedef struct _DT_FIXUP_PROTOCOL DT_FIXUP_PROTOCOL;

typedef EFI_STATUS (EFIAPI *DT_FIXUP) (
  IN DT_FIXUP_PROTOCOL *This,
  IN VOID              *Fdt,
  IN OUT UINTN         *BufferSize,
  IN UINT32             Flags
);

struct _DT_FIXUP_PROTOCOL {
  UINT64   Revision;
  DT_FIXUP Fixup;
};

#define STATUS_STR(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

STATIC LOG_SINK mLog;
STATIC BOOLEAN  mInstalled;

// Hands the splash over in the loader's tree on DT_APPLY_FIXUPS and installs
// it as the devicetree table on DT_INSTALL_TABLE. DT_RESERVE_MEMORY is a
// no-op: XBL's memory map already keeps the carveouts and display reserved.
STATIC EFI_STATUS EFIAPI
Fixup (
  IN DT_FIXUP_PROTOCOL *This,
  IN VOID              *Fdt,
  IN OUT UINTN         *BufferSize,
  IN UINT32             Flags
)
{
  UINT8 *Tree = Fdt;
  UINT32 Total;

  (VOID)This;
  if (!Tree || !BufferSize || *BufferSize < 8 ||
      ReadBe32 (Tree) != FDT_MAGIC) {
    return EFI_INVALID_PARAMETER;
  }
  Total = ReadBe32 (Tree + 4);
  if (Total < FDT_HEADER_SIZE) {
    return EFI_INVALID_PARAMETER;
  }
  // Nothing here grows the tree, so its own size is room enough.
  if (*BufferSize < Total) {
    *BufferSize = Total;
    return EFI_BUFFER_TOO_SMALL;
  }
  if (Flags & DT_APPLY_FIXUPS) {
    LogPrint (mLog, "dt-fixup: the loader's devicetree, %u bytes", Total);
    DisplayHandoverKeepSplash (SplashHandOver (Tree, Total, mLog));
  }
  if (Flags & DT_INSTALL_TABLE) {
    EFI_STATUS Status = gBS->InstallConfigurationTable (&gFdtTableGuid, Tree);

    LogPrint (
      mLog, "dt-fixup: the loader's devicetree installed as the table: %s",
      STATUS_STR (Status)
    );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  return EFI_SUCCESS;
}

STATIC DT_FIXUP_PROTOCOL mProtocol = {DT_FIXUP_REVISION, Fixup};

VOID
DtFixupInstall (IN LOG_SINK Log)
{
  EFI_HANDLE Handle = NULL;
  EFI_STATUS Status;

  mLog = Log;
  if (mInstalled) {
    return;
  }
  Status = gBS->InstallProtocolInterface (
    &Handle, &mDtFixupGuid, EFI_NATIVE_INTERFACE, &mProtocol
  );
  mInstalled = Status == EFI_SUCCESS;
  LogPrint (
    Log, "dt-fixup: EFI_DT_FIXUP_PROTOCOL for loaders that bring their own "
         "devicetree: %s",
    STATUS_STR (Status)
  );
}
