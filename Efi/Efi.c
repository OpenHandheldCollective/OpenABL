/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Efi - global tables, protocol helpers, GUIDs, status text and the clock.
*/

#include "Efi/Efi.h"

#include "Base/Memory.h"
#include "Base/Print.h"

EFI_HANDLE            gImageHandle;
EFI_SYSTEM_TABLE     *gST;
EFI_BOOT_SERVICES    *gBS;
EFI_RUNTIME_SERVICES *gRT;

VOID
UefiInit (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
)
{
  gImageHandle = ImageHandle;
  gST          = SystemTable;
  gBS          = SystemTable->BootServices;
  gRT          = SystemTable->RuntimeServices;
}

VOID *
GetProtocol (
  IN EFI_HANDLE Handle OPTIONAL,
  IN EFI_GUID  *Guid
)
{
  VOID *Interface = NULL;

  if (!Handle || EFI_ERROR (gBS->OpenProtocol (
                   Handle, Guid, &Interface, gImageHandle, NULL,
                   EFI_OPEN_PROTOCOL_GET_PROTOCOL
                 ))) {
    return NULL;
  }
  return Interface;
}

VOID *
LocateProtocol (IN EFI_GUID *Guid)
{
  VOID *Interface = NULL;

  if (EFI_ERROR (gBS->LocateProtocol (Guid, NULL, &Interface))) {
    return NULL;
  }
  return Interface;
}

UINTN
LocateHandles (
  IN EFI_GUID     *Guid,
  OUT EFI_HANDLE **Out
)
{
  UINTN Count = 0;

  *Out = NULL;
  if (EFI_ERROR (
        gBS->LocateHandleBuffer (ByProtocol, Guid, NULL, &Count, Out)
      )) {
    return 0;
  }
  return Count;
}

BOOLEAN
CompareGuid (
  IN CONST EFI_GUID *Guid1,
  IN CONST EFI_GUID *Guid2
)
{
  return CompareMem (Guid1, Guid2, sizeof (EFI_GUID)) == 0;
}

EFI_STATUS
StartImageFreeExitData (IN EFI_HANDLE Image)
{
  UINTN      Size   = 0;
  CHAR16    *Data   = NULL;
  EFI_STATUS Status = gBS->StartImage (Image, &Size, &Data);

  if (Data) {
    gBS->FreePool (Data);
  }
  return Status;
}

UINT8 *
MemoryMapGet (
  OUT UINTN *Count,
  OUT UINTN *DescriptorSize
)
{
  UINTN  Size    = 0;
  UINTN  MapKey  = 0;
  UINT32 Version = 0;
  UINT8 *Map;

  *Count = *DescriptorSize = 0;
  gBS->GetMemoryMap (&Size, NULL, &MapKey, DescriptorSize, &Version);
  if (*DescriptorSize < sizeof (EFI_MEMORY_DESCRIPTOR)) {
    return NULL;
  }
  // Room for the descriptors the allocation adds.
  Size += 8 * *DescriptorSize;
  Map = AllocatePool (((Size + 7) / 8) * 8);
  if (!Map) {
    return NULL;
  }
  if (EFI_ERROR (gBS->GetMemoryMap (
        &Size, (EFI_MEMORY_DESCRIPTOR *)Map, &MapKey, DescriptorSize, &Version
      ))) {
    FreePool (Map);
    return NULL;
  }
  *Count = Size / *DescriptorSize;
  return Map;
}

// GUIDs, by their edk2 names. An edk2 module gets these from AutoGen.c. The
// values come from the .dec files: EFIChipInfo.h's EFI_CHIPINFO_PROTOCOL_GUID
// macro isn't the GUID XBL installs.

EFI_GUID gEfiBlockIoProtocolGuid    = EFI_BLOCK_IO_PROTOCOL_GUID;
EFI_GUID gEfiDevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID gEfiDevicePathToTextProtocolGuid =
  EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID;
EFI_GUID gEfiDiskIoProtocolGuid              = EFI_DISK_IO_PROTOCOL_GUID;
EFI_GUID gEfiFileInfoGuid                    = EFI_FILE_INFO_ID;
EFI_GUID gEfiFileSystemInfoGuid              = EFI_FILE_SYSTEM_INFO_ID;
EFI_GUID gEfiFileSystemVolumeLabelInfoIdGuid = EFI_FILE_SYSTEM_VOLUME_LABEL_ID;
EFI_GUID gEfiGlobalVariableGuid              = EFI_GLOBAL_VARIABLE;
EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
EFI_GUID gEfiLoadFile2ProtocolGuid      = EFI_LOAD_FILE2_PROTOCOL_GUID;
EFI_GUID gEfiLoadedImageProtocolGuid    = EFI_LOADED_IMAGE_PROTOCOL_GUID;
EFI_GUID gEfiPartitionInfoProtocolGuid  = EFI_PARTITION_INFO_PROTOCOL_GUID;
EFI_GUID gEfiSerialIoProtocolGuid       = EFI_SERIAL_IO_PROTOCOL_GUID;
EFI_GUID gEfiSimpleFileSystemProtocolGuid =
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gEfiSimpleTextInProtocolGuid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
EFI_GUID gEfiSimpleTextInputExProtocolGuid =
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;

// Values from the .dec files.
EFI_GUID gQcomTokenSpaceGuid = {
  0x882f8c2b,
  0x9646,
  0x435f,
  {0x8d, 0xe5, 0xf2, 0x08, 0xff, 0x80, 0xc1, 0xbd}};
EFI_GUID gEfiChipInfoProtocolGuid = {
  0xb0760469,
  0x970c,
  0x487a,
  {0xa4, 0xb5, 0x28, 0xdb, 0x7b, 0x45, 0xce, 0xf1}};
EFI_GUID gEfiPlatformInfoProtocolGuid = {
  0x157a5c45,
  0x21b2,
  0x43c5,
  {0xba, 0x7c, 0x82, 0x2f, 0xee, 0x5f, 0xe5, 0x99}};
EFI_GUID gQcomPmicVersionProtocolGuid = {
  0x4684800a,
  0x2755,
  0x4edc,
  {0xb4, 0x43, 0x7f, 0x8c, 0xeb, 0x32, 0x39, 0xd3}};
EFI_GUID gEfiMemCardInfoProtocolGuid = {
  0x85c1f7d2,
  0xbce6,
  0x4f31,
  {0x8f, 0x4d, 0xd3, 0x7e, 0x03, 0xd0, 0x5e, 0xaa}};
EFI_GUID gEfiRamPartitionProtocolGuid = {
  0x5172FFB5,
  0x4253,
  0x7D51,
  {0xC6, 0x41, 0xA7, 0x01, 0xF9, 0x73, 0x10, 0x3C}};
EFI_GUID gQcomDisplayUtilsProtocolGuid = {
  0xc0dd69ac,
  0x76ba,
  0x11e6,
  {0xab, 0x24, 0x1f, 0xc7, 0xf5, 0x57, 0x5f, 0x19}};
EFI_GUID gEfiQcomVerifiedBootProtocolGuid =
  {0x8e5eff91, 0x21b6, 0x47d3, {0xaf, 0x2b, 0xc1, 0x5a, 0x1, 0xe0, 0x20, 0xec}};
EFI_GUID gChargerExProtocolGuid = {
  0x6edc8a6d,
  0x2663,
  0x43cd,
  {0x90, 0xff, 0x46, 0x21, 0xff, 0xd1, 0x0d, 0xf5}};
EFI_GUID gEfiDisplayPowerStateProtocolGuid = {
  0xf352021d,
  0x9593,
  0x4432,
  {0xbf, 0x04, 0x67, 0xb9, 0xf3, 0xb7, 0x60, 0x08}};
EFI_GUID gEfiDxeServicesTableGuid = {
  0x05AD34BA,
  0x6F02,
  0x4214,
  {0x95, 0x2E, 0x4D, 0xA0, 0x39, 0x8E, 0x2B, 0xB9}};
EFI_GUID gEfiHobListGuid = {0x7739F24C,
                            0x93D7,
                            0x11D4,
                            {0x9A, 0x3A, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D}};
EFI_GUID gFdtTableGuid   = {0xb1b621d5,
                            0xf19c,
                            0x41a5,
                            {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0}};
EFI_GUID gEfiCpuArchProtocolGuid = {
  0x26BACCB1,
  0x6F42,
  0x11D4,
  {0xBC, 0xE7, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81}};
EFI_GUID gEfiSecurityArchProtocolGuid = {
  0xA46423E3,
  0x4617,
  0x49F1,
  {0xB9, 0xFF, 0xD1, 0xBF, 0xA9, 0x11, 0x58, 0x39}};
EFI_GUID gEfiSecurity2ArchProtocolGuid = {
  0x94ab2f58,
  0x1438,
  0x4ef1,
  {0x91, 0x52, 0x18, 0x94, 0x1a, 0x3a, 0x0e, 0x68}};
EFI_GUID gEfiHiiFontProtocolGuid = {
  0xe9ca4775,
  0x8657,
  0x47fc,
  {0x97, 0xe7, 0x7e, 0xd6, 0x5a, 0x08, 0x43, 0x24}};
EFI_GUID gEfiHiiDatabaseProtocolGuid = {
  0xef9fc172,
  0xa1b2,
  0x4693,
  {0xb3, 0x27, 0x6d, 0x32, 0xfc, 0x41, 0x60, 0x42}};
EFI_GUID gEfiEventReadyToBootGuid = {
  0x7CE88FB3,
  0x4BD7,
  0x4679,
  {0x87, 0xA8, 0xA8, 0xD8, 0xDE, 0xE5, 0x0D, 0x2B}};
EFI_GUID gEfiEndOfDxeEventGroupGuid =
  {0x2ce967a, 0xdd7e, 0x4ffc, {0x9e, 0xe7, 0x81, 0xc, 0xf0, 0x47, 0x8, 0x80}};
EFI_GUID gEfiFirmwareVolume2ProtocolGuid =
  {0x220e73b6, 0x6bdb, 0x4413, {0x84, 0x5, 0xb9, 0x74, 0xb1, 0x8, 0x61, 0x9a}};
EFI_GUID gEdkiiNonDiscoverableDeviceProtocolGuid = {
  0x0d51905b,
  0xb77e,
  0x452a,
  {0xa2, 0xc0, 0xec, 0xa0, 0xcc, 0x8d, 0x51, 0x4a}};
EFI_GUID gEdkiiNonDiscoverableXhciDeviceGuid = {
  0xB1BE0BC5,
  0x6C28,
  0x442D,
  {0xAA, 0x37, 0x15, 0x1B, 0x42, 0x57, 0xBD, 0x78}};
EFI_GUID gEfiUsb2HcProtocolGuid = {
  0x3E745226,
  0x9818,
  0x45B6,
  {0xA2, 0xAC, 0xD7, 0xCD, 0x0E, 0x8B, 0xA2, 0xBC}};
EFI_GUID gEfiUsbIoProtocolGuid = {
  0x2B2F68D6,
  0x0CD2,
  0x44CF,
  {0x8E, 0x8B, 0xBB, 0xA2, 0x0B, 0x1B, 0x5B, 0x75}};
EFI_GUID gOpenAblMenuHostProtocolGuid = {
  0x6b3c9a52,
  0x1f4e,
  0x4d2a,
  {0x9c, 0x61, 0x2e, 0x7f, 0x0a, 0x5d, 0x83, 0xb4}};

CONST CHAR8 *
StatusText (
  IN EFI_STATUS Status,
  OUT CHAR8     Out[STATUS_TEXT_SIZE]
)
{
  // A warning is not "ok".
  if (Status == EFI_SUCCESS) {
    SPrint (Out, STATUS_TEXT_SIZE, "ok");
    return Out;
  }
  SPrint (
    Out, STATUS_TEXT_SIZE, "%s%u", EFI_ERROR (Status) ? "error " : "warning ",
    (UINT32)(Status & 0xFFFF)
  );
  return Out;
}

// Reads the virtual counter and its frequency in Hz. Both can be read at EL1
// and EL2.
STATIC VOID
Counter (
  OUT UINT64 *Count,
  OUT UINT64 *Frequency
)
{
  UINT64 Ticks;
  UINT64 Hz;

  __asm__ volatile ("isb\n\tmrs %0, cntvct_el0" : "=r"(Ticks));
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(Hz));
  *Count     = Ticks;
  *Frequency = Hz;
}

UINT64
ClockNowMs (VOID)
{
  UINT64 Count;
  UINT64 Frequency;

  Counter (&Count, &Frequency);
  return Frequency < 1000 ? 0 : Count / (Frequency / 1000);
}

UINT64
ClockNowUs (VOID)
{
  UINT64 Count;
  UINT64 Frequency;

  Counter (&Count, &Frequency);
  if (Frequency == 0) {
    return 0;
  }
  // Count * 1000000 / Frequency without 128-bit math, which would need a
  // compiler runtime. CNTFRQ_EL0 is 32 bits, so the remainder times 1000000
  // fits in 64.
  return Count / Frequency * 1000000 + Count % Frequency * 1000000 / Frequency;
}

VOID
InventoryConnectAll (VOID)
{
  EFI_HANDLE *Handles = NULL;
  // Empty driver list: just the NULL terminator.
  EFI_HANDLE NoDrivers[1] = {NULL};
  UINTN      Count        = 0;

  if (gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles) !=
      EFI_SUCCESS) {
    return;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    (VOID)gBS->ConnectController (Handles[Index], NoDrivers, NULL, TRUE);
  }
  gBS->FreePool (Handles);
}
