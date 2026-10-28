/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Qcom - calls into Qualcomm's XBL protocols, made as LinuxLoader makes them.
*/

#include "Efi/Qcom.h"

#include <Protocol/EFICardInfo.h>
#include <Protocol/EFIChipInfo.h>
#include <Protocol/EFIDisplayUtils.h>
#include <Protocol/EFIRamPartition.h>
#include <Protocol/EFIVerifiedBoot.h>

#include "Base/Memory.h"
#include "Core/Panel.h"

// Defined in Efi.c; LinuxLoaderLib.h has only the macro.
extern EFI_GUID gQcomTokenSpaceGuid;

// Values and layouts the calls rely on.
_Static_assert (
  EFI_DISPLAY_UTILS_PANEL_CONFIG == 3,
  "EFI_DISPLAY_UTILS_PANEL_CONFIG"
);
_Static_assert (
  sizeof (MEM_CARD_INFO) == 312,
  "MEM_CARD_INFO"
);

// Opens the protocol on the first handle that has it, shared as
// LocateProtocol would. NULL if there is none.
STATIC VOID *
Open (IN EFI_GUID *Guid)
{
  EFI_HANDLE *Handles   = NULL;
  UINTN       Count     = LocateHandles (Guid, &Handles);
  VOID       *Interface = Count ? GetProtocol (Handles[0], Guid) : NULL;

  if (Handles) {
    gBS->FreePool (Handles);
  }
  return Interface;
}

// Copies Bytes up to the first NUL into Out as UTF-8, replacing invalid
// sequences with U+FFFD. Out needs room for 3 * Size + 1 bytes.
STATIC VOID
LossyInto (
  OUT CHAR8      *Out,
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
)
{
  UINTN Written = 0;
  UINTN Len;
  UINTN End = 0;

  while (End < Size && Bytes[End]) {
    End++;
  }
  for (UINTN Index = 0; Index < End; Index += Len) {
    if (Utf8Decode ((CONST CHAR8 *)Bytes + Index, End - Index, &Len) ==
        0xfffd) {
      Out[Written++] = (CHAR8)0xef;
      Out[Written++] = (CHAR8)0xbf;
      Out[Written++] = (CHAR8)0xbd;
    } else {
      for (UINTN Index2 = 0; Index2 < Len; Index2++) {
        Out[Written++] = (CHAR8)Bytes[Index + Index2];
      }
    }
  }
  Out[Written] = 0;
}

_Static_assert (
  QCOM_CHIP_NAME_SIZE >= EFICHIPINFO_MAX_ID_LENGTH * 3 + 1,
  "QCOM_CHIP_NAME_SIZE"
);

BOOLEAN
QcomChip (OUT QCOM_CHIP *Out)
{
  EFI_CHIPINFO_PROTOCOL *ChipInfo = Open (&gEfiChipInfoProtocolGuid);
  EFIChipInfoIdType      Id       = 0;
  UINT32                 RawId    = 0;
  EFIChipInfoVersionType Version  = 0;
  UINT8                  Name[EFICHIPINFO_MAX_ID_LENGTH] = {0};

  *Out = (QCOM_CHIP){0};
  if (!ChipInfo) {
    return FALSE;
  }
  // Same calls, in the same order, as LinuxLoader's Board.c.
  Out->Id.Status      = ChipInfo->GetChipId (ChipInfo, &Id);
  Out->RawId.Status   = ChipInfo->GetRawChipId (ChipInfo, &RawId);
  Out->Version.Status = ChipInfo->GetChipVersion (ChipInfo, &Version);
  Out->NameStatus =
    ChipInfo->GetChipIdString (ChipInfo, (CHAR8 *)Name, sizeof (Name));
  Out->Revision      = ChipInfo->Revision;
  Out->Id.Value      = (UINT32)Id;
  Out->RawId.Value   = RawId;
  Out->Version.Value = Version;
  LossyInto (Out->Name, Name, sizeof (Name));
  return TRUE;
}

BOOLEAN
QcomPlatform (OUT EFI_PLATFORMINFO_PLATFORM_INFO_TYPE *Out)
{
  EFI_PLATFORMINFO_PROTOCOL *PlatformInfo =
    Open (&gEfiPlatformInfoProtocolGuid);

  *Out = (EFI_PLATFORMINFO_PLATFORM_INFO_TYPE){0};
  return PlatformInfo &&
         PlatformInfo->GetPlatformInfo (PlatformInfo, Out) == EFI_SUCCESS;
}

UINTN
QcomPmics (OUT EFI_PM_DEVICE_INFO_TYPE Out[QCOM_PMICS_MAX])
{
  EFI_QCOM_PMIC_VERSION_PROTOCOL *PmicVersion =
    Open (&gQcomPmicVersionProtocolGuid);
  UINTN Count = 0;

  if (!PmicVersion) {
    return 0;
  }
  for (UINT32 Index = 0; Index < QCOM_PMICS_MAX; Index++) {
    EFI_PM_DEVICE_INFO_TYPE Info = {0};

    if (PmicVersion->GetPmicInfo (Index, &Info) != EFI_SUCCESS) {
      break;
    }
    // Model 0 is unknown and 0xFFFF and up invalid: no PMIC at this index.
    if ((UINT32)Info.PmicModel != 0 && (UINT32)Info.PmicModel < 0xFFFF) {
      Out[Count++] = Info;
    }
  }
  return Count;
}

// Converts a MEM_CARD_INFO text field: up to a NUL, invalid UTF-8 replaced,
// words split at space, tab and newline and joined by single spaces. NULL
// when out of memory.
STATIC CHAR8 *
CardText (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size
)
{
  UINTN       End = 0;
  CHAR8      *Lossy;
  OABL_STRING Out = {0};
  OABL_TEXT   Rest;

  while (End < Size && Bytes[End]) {
    End++;
  }
  Lossy = Utf8Lossy (Bytes, End);
  if (!Lossy) {
    return NULL;
  }
  Rest = TextOf (Lossy);
  for (UINTN WordStart = 0; WordStart <= Rest.Len;) {
    UINTN WordEnd = WordStart;

    while (WordEnd < Rest.Len && Rest.Ptr[WordEnd] != ' ' &&
           Rest.Ptr[WordEnd] != '\t' && Rest.Ptr[WordEnd] != '\n') {
      WordEnd++;
    }
    if (WordEnd > WordStart) {
      if (Out.Len) {
        StringAppendChar (&Out, ' ');
      }
      StringAppendN (&Out, Rest.Ptr + WordStart, WordEnd - WordStart);
    }
    WordStart = WordEnd + 1;
  }
  FreePool (Lossy);
  if (Out.Failed) {
    StringFree (&Out);
    return NULL;
  }
  return StringTake (&Out);
}

VOID
QcomCards (IN OUT QCOM_CARDS *Out)
{
  EFI_HANDLE *Handles = NULL;
  UINTN       Count   = LocateHandles (&gEfiMemCardInfoProtocolGuid, &Handles);

  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_MEM_CARDINFO_PROTOCOL *CardInfo =
      GetProtocol (Handles[Index], &gEfiMemCardInfoProtocolGuid);
    EFI_BLOCK_IO_PROTOCOL *BlockIo;
    MEM_CARD_INFO          Raw = {0};
    QCOM_CARD              Card;

    if (!CardInfo) {
      continue;
    }
    if (CardInfo->GetCardInfo (CardInfo, &Raw) != EFI_SUCCESS) {
      continue;
    }
    BlockIo = GetProtocol (Handles[Index], &gEfiBlockIoProtocolGuid);
    Card    = (QCOM_CARD){
      .Kind      = CardText (Raw.card_type, sizeof (Raw.card_type)),
      .MfrId     = Raw.mfr_id,
      .OemId     = Raw.oem_id,
      .Inquiry   = CardText (Raw.inquiry_str, sizeof (Raw.inquiry_str)),
      .Removable = BlockIo && BlockIo->Media && BlockIo->Media->RemovableMedia,
    };
    if (!Card.Kind || !Card.Inquiry || !VecPush (Out, Card)) {
      FreePool (Card.Kind);
      FreePool (Card.Inquiry);
    }
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
}

VOID
QcomCardsFree (IN OUT QCOM_CARDS *Cards)
{
  for (UINTN Index = 0; Index < Cards->Count; Index++) {
    FreePool (Cards->Items[Index].Kind);
    FreePool (Cards->Items[Index].Inquiry);
  }
  VecFree (Cards);
}

CHAR8 *
QcomDisplayPanelConfig (VOID)
{
  STATIC CHAR16 Name[] = L"DisplayPanelConfiguration";
  UINT32        Attr   = 0;
  UINTN         Size   = 0;
  UINT8        *Data;
  EFI_STATUS    Status;
  CHAR8        *Lossy;
  CHAR8        *Out;
  UINTN         End = 0;

  Status = gRT->GetVariable (Name, &gQcomTokenSpaceGuid, &Attr, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return NULL;
  }
  Data = AllocatePool (Size ? Size : 1);
  if (!Data) {
    return NULL;
  }
  if (gRT->GetVariable (Name, &gQcomTokenSpaceGuid, &Attr, &Size, Data) !=
      EFI_SUCCESS) {
    FreePool (Data);
    return NULL;
  }
  while (End < Size && Data[End]) {
    End++;
  }
  Lossy = Utf8Lossy (Data, End);
  FreePool (Data);
  if (!Lossy) {
    return NULL;
  }
  Out = TextDup (TextTrim (TextOf (Lossy)));
  FreePool (Lossy);
  return Out;
}

BOOLEAN
QcomRamBanks (OUT QCOM_BANKS *Out)
{
  EFI_RAMPARTITION_PROTOCOL *RamPartition =
    Open (&gEfiRamPartitionProtocolGuid);
  RamPartitionEntry *Entries;
  UINT32             Count = 0;
  UINT32             Capacity;
  EFI_STATUS         Status;

  *Out = (QCOM_BANKS){0};
  if (!RamPartition) {
    return FALSE;
  }
  // As in Board.c: ask for the count, then for that many entries.
  Status = RamPartition->GetRamPartitions (RamPartition, NULL, &Count);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return FALSE;
  }
  Capacity = Count;
  Entries  = AllocateZeroArray (Capacity ? Capacity : 1, sizeof (*Entries));
  if (!Entries) {
    return FALSE;
  }
  Status = RamPartition->GetRamPartitions (RamPartition, Entries, &Count);
  if (Status != EFI_SUCCESS || Count == 0) {
    FreePool (Entries);
    return FALSE;
  }
  if (Count > Capacity) {
    Count = Capacity;
  }
  if (!VecReserve (Out, Count)) {
    FreePool (Entries);
    return FALSE;
  }
  for (UINT32 Index = 0; Index < Count; Index++) {
    Out->Items[Out->Count++] =
      (CARVEOUTS_REGION){Entries[Index].Base, Entries[Index].AvailableLength};
  }
  FreePool (Entries);
  return TRUE;
}

BOOLEAN
QcomReadDeviceState (
  IN UINTN         Length,
  OUT UINT64      *Revision,
  OUT EFI_STATUS  *Status,
  OUT OABL_BUFFER *Out
)
{
  QCOM_VERIFIEDBOOT_PROTOCOL *VerifiedBoot =
    Open (&gEfiQcomVerifiedBootProtocolGuid);

  *Out = (OABL_BUFFER){0};
  if (!VerifiedBoot || !BufferAppendZeros (Out, Length)) {
    return FALSE;
  }
  *Status = VerifiedBoot->VBRwDeviceState (
    VerifiedBoot, READ_CONFIG, Out->Data, (UINT32)Length
  );
  *Revision = VerifiedBoot->Revision;
  return TRUE;
}

BOOLEAN
QcomFirmwarePanelRotation (
  IN UINTN    Width,
  IN UINTN    Height,
  OUT UINT16 *Out
)
{
  QCOM_XMLS Xmls;
  BOOLEAN   Ok;

  if (!QcomPanelXmls (&Xmls)) {
    return FALSE;
  }
  Ok = PanelXmlFirmwareRotation (
    Xmls.Items, Xmls.Count, (UINT32)Width, (UINT32)Height, Out
  );
  VecFree (&Xmls);
  return Ok;
}

BOOLEAN
QcomPanelXmls (OUT QCOM_XMLS *Out)
{
  EfiQcomDisplayUtilsProtocol *DisplayUtils =
    Open (&gQcomDisplayUtilsProtocolGuid);

  *Out = (QCOM_XMLS){0};
  if (!DisplayUtils) {
    return FALSE;
  }
  // XBL lists a few dozen panels; it answers EFI_UNSUPPORTED past the end.
  for (UINT32 Index = 0; Index < 256; Index++) {
    EFI_DISPLAY_UTILS_PANEL_CONFIG_PARAMS Params = {.uPanelIndex = Index};
    UINT32                                Size   = sizeof (Params);

    // XBL points pPanelConfig at XML it keeps until ExitBootServices.
    if (DisplayUtils->DisplayUtilsGetProperty (
          EFI_DISPLAY_UTILS_PANEL_CONFIG, &Params, &Size
        ) != EFI_SUCCESS) {
      break;
    }
    if (Params.pPanelConfig && Params.uPanelConfigLength > 0 &&
        !VecPush (
          Out, BytesOf (Params.pPanelConfig, Params.uPanelConfigLength)
        )) {
      break;
    }
  }
  return TRUE;
}
