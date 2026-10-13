/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SysInfo - the system facts on the menu's info card.
*/

#include "Efi/SysInfo.h"

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Guid/HobList.h>

#include "Base/Memory.h"
#include "Base/Buffer.h"
#include "Base/Text.h"
#include "Core/Gpt.h"
#include "Core/Panel.h"
#include "Core/SysInfoText.h"
#include "Efi/Qcom.h"
#include "Efi/Uninstall.h"

#define HOB_RESOURCE_DESCRIPTOR  0x0003
#define HOB_END                  0xFFFF
#define RESOURCE_SYSTEM_MEMORY   0
#define RESOURCE_MEMORY_RESERVED 5

CHAR8 *
SysInfoSoc (VOID)
{
  QCOM_CHIP    Chip;
  CONST CHAR8 *Name;

  if (!QcomChip (&Chip)) {
    return NULL;
  }
  if ((Name =
         SysInfoSocName (Chip.Id.Status == EFI_SUCCESS ? Chip.Id.Value : 0))) {
    return AsciiStrDup (Name);
  }
  return Chip.NameStatus == EFI_SUCCESS && Chip.Name[0]
           ? AsciiStrDup (Chip.Name)
           : NULL;
}

// Sums the system and reserved memory in XBL's resource HOBs.
STATIC UINT64
HobMemory (VOID)
{
  CONST UINT8 *Hob   = NULL;
  UINT64       Total = 0;

  for (UINTN Index = 0; Index < gST->NumberOfTableEntries && !Hob; Index++) {
    if (CompareGuid (
          &gST->ConfigurationTable[Index].VendorGuid, &gEfiHobListGuid
        )) {
      Hob = gST->ConfigurationTable[Index].VendorTable;
    }
  }
  if (!Hob) {
    return 0;
  }
  // Walk the HOB list XBL handed to DXE. A resource descriptor has its type
  // at 24 and its length at 40.
  for (INT32 Index = 0; Index < 65536; Index++) {
    UINT16 Type   = ReadLe16 (Hob);
    UINTN  Length = ReadLe16 (Hob + 2);

    if (Type == HOB_END || Length < 8) {
      break;
    }
    if (Type == HOB_RESOURCE_DESCRIPTOR && Length >= 48) {
      UINT32 Kind = ReadLe32 (Hob + 24);

      if (Kind == RESOURCE_SYSTEM_MEMORY || Kind == RESOURCE_MEMORY_RESERVED) {
        Total += ReadLe64 (Hob + 40);
      }
    }
    Hob += Length;
  }
  return Total;
}

// Sums the sizes of the raw disks (not partitions) with media present,
// fixed or removable.
STATIC UINT64
Disks (IN BOOLEAN Removable)
{
  EFI_HANDLE *Handles;
  UINTN       Count = LocateHandles (&gEfiBlockIoProtocolGuid, &Handles);
  UINT64      Total = 0;

  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL *BlockIo =
      GetProtocol (Handles[Index], &gEfiBlockIoProtocolGuid);
    EFI_BLOCK_IO_MEDIA *Media;

    if (!BlockIo) {
      continue;
    }
    Media = BlockIo->Media;
    if (Media->MediaPresent && (BOOLEAN)Media->RemovableMedia == Removable &&
        !Media->LogicalPartition) {
      Total += (Media->LastBlock + 1) * (UINT64)Media->BlockSize;
    }
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  return Total;
}

CHAR8 *
SysInfoRam (VOID)
{
  UINT64 Bytes = HobMemory ();

  return Bytes > 0 ? SysInfoRamSize (Bytes) : NULL;
}

CHAR8 *
SysInfoStorage (VOID)
{
  UINT64 Bytes = Disks (FALSE);

  return Bytes > 0 ? SysInfoStorageSize (Bytes) : NULL;
}

CHAR8 *
SysInfoSdCard (VOID)
{
  UINT64 Bytes = Disks (TRUE);

  return Bytes > 0 ? SysInfoCardSize (Bytes) : NULL;
}

VOID
SysInfoRawSizes (
  OUT UINT64 *Ram,
  OUT UINT64 *Storage,
  OUT UINT64 *Card
)
{
  *Ram     = HobMemory ();
  *Storage = Disks (FALSE);
  *Card    = Disks (TRUE);
}

CHAR8 *
SysInfoPanel (
  IN UINTN Width,
  IN UINTN Height
)
{
  QCOM_XMLS Xmls = {0};
  OABL_TEXT Name;
  CHAR8    *Config = QcomDisplayPanelConfig ();
  CHAR8    *Panel  = NULL;

  if (Config) {
    Panel = SysInfoPanelFromCmdline (TextOf (Config));
    FreePool (Config);
    if (Panel) {
      return Panel;
    }
  }
  if (!QcomPanelXmls (&Xmls)) {
    return NULL;
  }
  if (PanelXmlNameAt (
        Xmls.Items, Xmls.Count, (UINT32)Width, (UINT32)Height, &Name
      )) {
    Panel = TextDup (Name);
  }
  VecFree (&Xmls);
  return Panel;
}

// vbmeta partitions to read the Android build from, in order of preference.
STATIC CONST CHAR8 *CONST mVbmetas[6] = {"vbmeta_system_a", "vbmeta_system_b",
                                         "vbmeta_system",   "vbmeta_a",
                                         "vbmeta_b",        "vbmeta"};
#define N_VBMETAS (sizeof (mVbmetas) / sizeof (*mVbmetas))
// The build properties AVB signs into them, in order.
STATIC CONST CHAR8 *CONST mFingerprints[3] = {
  "com.android.build.system.fingerprint",
  "com.android.build.vendor.fingerprint",
  "com.android.build.boot.fingerprint",
};
// vbmeta images are at most this big.
#define VBMETA_MAX ((UINTN)64 << 10)

// Whether a GPT entry's name (36 UTF-16 units) is Want, ending at the
// field's end or a NUL.
STATIC BOOLEAN
NameIs (
  IN CONST UINT8 *Name,
  IN CONST CHAR8 *Want
)
{
  UINTN Index = 0;

  for (; Want[Index]; Index++) {
    if (Index >= 36 || ReadLe16 (Name + 2 * Index) != (UINT8)Want[Index]) {
      return FALSE;
    }
  }
  return Index == 36 || ReadLe16 (Name + 2 * Index) == 0;
}

// A vbmeta partition: its rank in mVbmetas, its disk and first block.
typedef struct _VBMETA_AT {
  UINTN      Rank;
  EFI_HANDLE Disk;
  UINT64     Lba;
} VBMETA_AT;

typedef struct _VBMETA_LIST OABL_VEC (VBMETA_AT) VBMETA_LIST;

CHAR8 *
SysInfoAndroidBuild (VOID)
{
  VBMETA_LIST Found = {0};
  EFI_HANDLE *Handles;
  UINTN       Count = LocateHandles (&gEfiBlockIoProtocolGuid, &Handles);
  CHAR8      *Build = NULL;

  if (!Count) {
    return NULL;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL *BlockIo = UninstallBlockIo (Handles[Index]);
    OABL_BUFFER            Block   = {0};
    OABL_BUFFER            Array   = {0};
    GPT_HEADER             Header;
    UINTN                  Size;

    if (!BlockIo || !UninstallTable (BlockIo, &Block, &Header, &Array)) {
      BufferFree (&Block);
      BufferFree (&Array);
      continue;
    }
    Size = Header.EntrySize;
    for (UINTN Index2 = 0;
         Size && Index2 < Header.Entries && Index2 * Size < Array.Len;
         Index2++) {
      CONST UINT8 *Entry = Array.Data + Index2 * Size;
      UINTN        Have =
        Array.Len - Index2 * Size < Size ? Array.Len - Index2 * Size : Size;
      UINT64 First;
      UINT64 Last;
      UINTN  Rank;

      if (Have < 128) {
        continue;
      }
      for (Rank = 0; Rank < N_VBMETAS && !NameIs (Entry + 56, mVbmetas[Rank]);
           Rank++)
        ;
      if (Rank < N_VBMETAS &&
          GptExtent (&Header, Array.Data, Array.Len, Index2, &First, &Last)) {
        VecPush (&Found, (VBMETA_AT){Rank, Handles[Index], First});
      }
    }
    BufferFree (&Block);
    BufferFree (&Array);
  }
  gBS->FreePool (Handles);
  for (UINTN Rank = 0; Rank < N_VBMETAS && !Build; Rank++) {
    for (UINTN Index = 0; Index < Found.Count && !Build; Index++) {
      EFI_BLOCK_IO_PROTOCOL *BlockIo;
      OABL_BUFFER            Image = {0};

      if (Found.Items[Index].Rank != Rank ||
          !(BlockIo = UninstallBlockIo (Found.Items[Index].Disk))) {
        continue;
      }
      if (UninstallRead (BlockIo, Found.Items[Index].Lba, VBMETA_MAX, &Image)) {
        Build = SysInfoVbmetaProperty (Image.Data, Image.Len, mFingerprints, 3);
      }
      BufferFree (&Image);
    }
  }
  VecFree (&Found);
  return Build;
}
