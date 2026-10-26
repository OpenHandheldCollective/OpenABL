/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-source-exception */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/*
 * OpenAblMenu - starts the OpenABL program from the ABL's firmware volume:
 * early to apply its XBL quirks, then to run the boot, menu and fastboot.
 */

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BootLinux.h>
#include <Library/DebugLib.h>
#include <Library/Decompress.h>
#include <Library/FdtRw.h>
#include <Library/DeviceInfo.h>
#include <Library/HypervisorMvCalls.h>
#include <Library/KeyPad.h>
#include <Library/MenuKeysDetection.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PartitionTableUpdate.h>
#include <Library/OpenAblMenu.h>
#include <Library/ShutdownServices.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/Rtic.h>
#include <Library/UpdateCmdLine.h>
#include <Library/UpdateDeviceTree.h>
#include <Library/VerifiedBoot.h>
#include <FastbootLib/FastbootMain.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/OpenAblAblLib.h>
#include <Protocol/OpenAblMenuHost.h>
#include <libfdt.h>

/* FILE GUID of openabl-menu.efi in QcomModulePkg.fdf. */
STATIC EFI_GUID mOpenAblMenuFileGuid = {
    0x3f1d0c7a, 0x8e52, 0x4b6d, {0xa9, 0x3e, 0x51, 0xc2, 0x7d, 0x04, 0xb8, 0x6f}};
STATIC EFI_GUID mOpenAblStorageGuid = OPENABL_STORAGE_PROTOCOL_GUID;
STATIC EFI_GUID mOpenAblAblLibGuid = OPENABL_ABL_LIB_PROTOCOL_GUID;

/* The ABL library, and the menu host while the program runs. */
STATIC EFI_HANDLE mHostHandle;

STATIC BOOLEAN mFastbootUp;
/* What LinuxLoaderEntry found. */
STATIC BOOLEAN  mMainRun;
STATIC BootInfo mInfo;
STATIC UINT8    mEntry;
STATIC UINT16   mKey;
STATIC BOOLEAN  mSetRot;
/* Qualcomm's code reads these later, for the A/B retry count. */
STATIC BOOLEAN  *mFastboot;
STATIC BOOLEAN  *mRecovery;

#if HIBERNATION_SUPPORT_NO_AES
VOID BootIntoHibernationImage (BootInfo *Info,
                               BOOLEAN *SetRotAndBootStateAndVBH);
#endif

STATIC OPENABL_STORAGE_PROTOCOL *LoadedProgram (VOID);

/* OpenAblSerialPortLib's. */
extern OPENABL_STORAGE_LOG_SINK gOpenAblLogSink;

STATIC VOID
CopyText (CHAR8 *Dst, UINTN DstSize, CONST CHAR8 *Src)
{
  if ((Src == NULL) || (DstSize == 0)) {
    return;
  }
  AsciiStrnCpyS (Dst, DstSize, Src, DstSize - 1);
}

STATIC EFI_STATUS EFIAPI
HostGetState (IN OPENABL_MENU_HOST_PROTOCOL *This, OUT OPENABL_MENU_STATE *State)
{
  UINT8 Flags = 0;

  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (State, sizeof (*State));
  State->OffmodeCharging = IsChargingScreenEnable () ? 1 : 0;

#if ABL_USB_HOST
  Flags |= OPENABL_MENU_STATE_USB_HOST;
#endif
  if (FixedPcdGetBool (EnableForceBootAlternateSlot) && IsSlotsUbootable ()) {
    Flags |= OPENABL_MENU_STATE_ALTERNATE_SLOT;
  }
  if (IsVmEnabled ()) {
    Flags |= OPENABL_MENU_STATE_VM_ENABLED;
  }
  State->Flags = Flags;
  State->Entry = mMainRun ? mEntry : 0;
  State->Key = mMainRun ? mKey : SCAN_NULL;
  CopyText (State->Version, sizeof (State->Version), PRODUCT_VERSION);
  return EFI_SUCCESS;
}

/* Returns only if the boot failed. */
STATIC EFI_STATUS
BootAndroid (IN BOOLEAN Recovery)
{
  BootInfo   Info = mInfo;
  EFI_STATUS Status;

  Info.BootIntoRecovery = Recovery;
  *mRecovery = Recovery;
  /* An earlier Linux boot may have left FdtRw's cache stale. */
  FdtResetNodeList ();
#if HIBERNATION_SUPPORT_NO_AES
  BootIntoHibernationImage (&Info, &mSetRot);
#endif
  Status = LoadImageAndAuth (&Info, FALSE, mSetRot
#ifndef USE_DUMMY_BCC
                             , &BccParamsRecvdFromAVB
#endif
                            );
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "LoadImageAndAuth failed: %r\n", Status));
    return Status;
  }

  BootLinux (&Info);
  return EFI_LOAD_ERROR;
}

STATIC EFI_STATUS EFIAPI
HostDoAction (IN OPENABL_MENU_HOST_PROTOCOL *This, IN UINT32 Action)
{
  EFI_STATUS Status;

  DEBUG ((EFI_D_ERROR, "OpenAblMenu: action %u\n", Action));
  switch (Action) {
  case OPENABL_MENU_ACTION_START:
    RebootDevice (NORMAL_MODE);
    return EFI_DEVICE_ERROR; /* the reset failed */
  case OPENABL_MENU_ACTION_RESTART_BOOTLOADER:
    RebootDevice (FASTBOOT_MODE);
    return EFI_DEVICE_ERROR;
  case OPENABL_MENU_ACTION_RECOVERY:
    RebootDevice (RECOVERY_MODE);
    return EFI_DEVICE_ERROR;
  case OPENABL_MENU_ACTION_EMERGENCY:
    RebootDevice (EMERGENCY_DLOAD);
    return EFI_DEVICE_ERROR;
  case OPENABL_MENU_ACTION_POWER_OFF:
    ShutdownDevice ();
    return EFI_DEVICE_ERROR;
  case OPENABL_MENU_ACTION_ALTERNATE_SLOT:
    if (!FixedPcdGetBool (EnableForceBootAlternateSlot) || !IsSlotsUbootable ()) {
      return EFI_UNSUPPORTED;
    }
    return HandleActiveSlotUnbootable (TRUE);
  default:
    break;
  }
  /* The rest are only offered while the program runs the boot. */
  if (!mMainRun) {
    return EFI_UNSUPPORTED;
  }
  switch (Action) {
  case OPENABL_MENU_ACTION_FASTBOOT_START:
    /* Delete enters fastboot like Volume Down. */
    if (mKey == SCAN_DELETE) {
      *mFastboot = TRUE;
    }
    Status = FastbootStart ();
    mFastbootUp = !EFI_ERROR (Status);
    return Status;
  case OPENABL_MENU_ACTION_FASTBOOT_STOP:
    if (!mFastbootUp) {
      return EFI_SUCCESS;
    }
    mFastbootUp = FALSE;
    return FastbootStop ();
  case OPENABL_MENU_ACTION_FASTBOOT_MENU:
    FastbootTextMenuLoop ();
    return EFI_SUCCESS;
  case OPENABL_MENU_ACTION_BOOT_PREP:
    ExitMenuKeysDetection ();
    OpenAblBootPrep (OPENABL_PREP_BOOT);
    return EFI_SUCCESS;
  case OPENABL_MENU_ACTION_BOOT_ANDROID:
  case OPENABL_MENU_ACTION_BOOT_ANDROID_RECOVERY:
    return BootAndroid (Action ==
                               OPENABL_MENU_ACTION_BOOT_ANDROID_RECOVERY);
  case OPENABL_MENU_ACTION_CHARGER_BOOT: {
    BOOLEAN Charger = FALSE;

    TargetPauseForBatteryCharge (&Charger);
    return Charger ? EFI_SUCCESS : EFI_NOT_FOUND;
  }
  case OPENABL_MENU_ACTION_CHARGER_SCREEN_ON:
  case OPENABL_MENU_ACTION_CHARGER_SCREEN_OFF:
    return EnableChargingScreen (Action ==
                                 OPENABL_MENU_ACTION_CHARGER_SCREEN_ON);
  case OPENABL_MENU_ACTION_GADGET_PAUSE:
  case OPENABL_MENU_ACTION_GADGET_RESUME:
    return !mFastbootUp
             ? EFI_UNSUPPORTED
             : FastbootGadget (Action == OPENABL_MENU_ACTION_GADGET_RESUME);
  default:
    return EFI_UNSUPPORTED;
  }
}

STATIC EFI_STATUS EFIAPI
HostPoll (IN OPENABL_MENU_HOST_PROTOCOL *This)
{
  return mFastbootUp ? FastbootPoll () : EFI_SUCCESS;
}

STATIC VOID EFIAPI
HostLog (IN OPENABL_MENU_HOST_PROTOCOL *This, IN CONST CHAR8 *Line)
{
  if (Line != NULL) {
    DEBUG ((EFI_D_ERROR, "%a\n", Line));
  }
}

STATIC EFI_STATUS EFIAPI
LibInflate (IN CONST OPENABL_ABL_LIB_PROTOCOL *This, IN CONST VOID *In,
            IN UINTN InSize, OUT VOID *Out, IN UINTN OutSize,
            OUT UINTN *Written, OUT UINTN *Used)
{
  UINT32 Pos = 0;
  UINT32 Len = 0;

  if ((In == NULL) || (Out == NULL) || (Written == NULL) || (Used == NULL) ||
      (InSize > MAX_UINT32) || (OutSize > MAX_UINT32)) {
    return EFI_INVALID_PARAMETER;
  }
  if (decompress ((UINT8 *)In, (UINT32)InSize, Out, (UINT32)OutSize, &Pos,
                  &Len) != 0) {
    return EFI_LOAD_ERROR;
  }
  *Written = Len;
  *Used = Pos;
  return EFI_SUCCESS;
}

STATIC OPENABL_ABL_LIB_PROTOCOL mAblLib = {
    LibInflate, fdt_path_offset, fdt_get_name, fdt_getprop, fdt_next_node,
    fdt_first_subnode, fdt_next_subnode, fdt_node_offset_by_compatible,
    fdt_setprop, fdt_add_subnode};

STATIC OPENABL_MENU_HOST_PROTOCOL mHost = {
    0, HostGetState, HostDoAction, HostPoll, HostLog};

/* EFI_NOT_FOUND if this build doesn't include the program. */
STATIC EFI_STATUS
StartMenu (IN UINT32 Flags)
{
  EFI_LOADED_IMAGE_PROTOCOL     *Self = NULL;
  EFI_FIRMWARE_VOLUME2_PROTOCOL *Fv = NULL;
  EFI_HANDLE                     HostHandle = mHostHandle;
  EFI_HANDLE                     Image = NULL;
  VOID                          *Pe32 = NULL;
  UINTN                          Pe32Size = 0;
  UINT32                         AuthStatus = 0;
  OPENABL_STORAGE_PROTOCOL      *Program = NULL;
  EFI_STATUS                     Status;

  /* Reuse the copy the early start left loaded. */
  if ((Flags & OPENABL_MENU_FLAG_EARLY) == 0) {
    Program = LoadedProgram ();
  }
  if (Program == NULL) {
    Status = gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid,
                                  (VOID **)&Self);
    if (EFI_ERROR (Status) || (Self == NULL) || (Self->DeviceHandle == NULL)) {
      return EFI_NOT_FOUND;
    }
    Status = gBS->HandleProtocol (Self->DeviceHandle,
                                  &gEfiFirmwareVolume2ProtocolGuid, (VOID **)&Fv);
    if (EFI_ERROR (Status) || (Fv == NULL)) {
      DEBUG ((EFI_D_ERROR, "OpenAblMenu: no firmware volume: %r\n", Status));
      return EFI_NOT_FOUND;
    }
    Status = Fv->ReadSection (Fv, &mOpenAblMenuFileGuid, EFI_SECTION_PE32, 0,
                              &Pe32, &Pe32Size, &AuthStatus);
    if (EFI_ERROR (Status) || (Pe32 == NULL)) {
      DEBUG ((EFI_D_ERROR, "OpenAblMenu: not in this build: %r\n", Status));
      return EFI_NOT_FOUND;
    }
  }

  mHost.Flags = Flags;
  Status = gBS->InstallProtocolInterface (&HostHandle,
                                          &gOpenAblMenuHostProtocolGuid,
                                          EFI_NATIVE_INTERFACE, &mHost);
  if (EFI_ERROR (Status)) {
    if (Pe32 != NULL) {
      FreePool (Pe32);
    }
    DEBUG ((EFI_D_ERROR, "OpenAblMenu: installing the host failed: %r\n", Status));
    return Status;
  }

  if (Program != NULL) {
    DEBUG ((EFI_D_ERROR, "OpenAblMenu: running (flags 0x%x)\n", Flags));
    Status = Program->Run (Program);
  } else {
    Status = gBS->LoadImage (FALSE, gImageHandle, NULL, Pe32, Pe32Size, &Image);
    FreePool (Pe32);
    if (!EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "OpenAblMenu: starting (%u bytes)\n", (UINT32)Pe32Size));
      Status = gBS->StartImage (Image, NULL, NULL);
    }
  }
  gBS->UninstallProtocolInterface (HostHandle, &gOpenAblMenuHostProtocolGuid,
                                   &mHost);
  DEBUG ((EFI_D_ERROR, "OpenAblMenu: returned %r\n", Status));
  return Status;
}

EFI_STATUS
OpenAblMenuMain (IN CONST BootInfo *Info, IN OUT BOOLEAN *Fastboot,
                 IN OUT BOOLEAN *Recovery, IN UINT32 Key,
                 IN BOOLEAN SetRotAndBootStateAndVBH)
{
  EFI_STATUS Status;

  /* Only the copy left loaded by the early start can run the boot. */
  if (LoadedProgram () == NULL) {
    return EFI_UNSUPPORTED;
  }
  mInfo = *Info;
  mEntry = (*Fastboot ? OPENABL_MENU_ENTRY_FASTBOOT : 0) |
           (*Recovery ? OPENABL_MENU_ENTRY_RECOVERY : 0);
  mFastboot = Fastboot;
  mRecovery = Recovery;
  mKey = (UINT16)Key;
  mSetRot = SetRotAndBootStateAndVBH;
  mMainRun = TRUE;
  Status = StartMenu (OPENABL_MENU_FLAG_MAIN);
  mMainRun = FALSE;
  return Status;
}

VOID
OpenAblMenuEarly (VOID)
{
  OPENABL_STORAGE_PROTOCOL *Storage;
  EFI_STATUS                Status;

  Status = gBS->InstallProtocolInterface (&mHostHandle, &mOpenAblAblLibGuid,
                                          EFI_NATIVE_INTERFACE, &mAblLib);
  if (EFI_ERROR (Status)) {
    mHostHandle = NULL;
    DEBUG ((EFI_D_ERROR, "OpenAblMenu: installing the ABL library failed: %r\n",
            Status));
  }

  Status = StartMenu (OPENABL_MENU_FLAG_EARLY);
  /* From here on the program takes the ABL's debug output. */
  Storage = LoadedProgram ();
  if (Storage != NULL) {
    gOpenAblLogSink = Storage->LogSink;
  }
  DEBUG ((EFI_D_ERROR, "OpenAblMenu: early: %r\n", Status));
}

STATIC VOID EFIAPI
StorageLog (IN CONST CHAR8 *Line)
{
  if (Line != NULL) {
    DEBUG ((EFI_D_ERROR, "%a\n", Line));
  }
}

VOID
OpenAblBootPrep (IN UINT32 Flags)
{
  OPENABL_STORAGE_PROTOCOL *Storage;

  Storage = LoadedProgram ();
  if (Storage == NULL) {
    DEBUG ((EFI_D_ERROR, "OpenAblMenu: no OpenABL program to get storage ready\n"));
    return;
  }
  Storage->BootPrep (Storage, Flags, StorageLog);
}

/* The storage protocol of the program the early start left loaded. */
STATIC OPENABL_STORAGE_PROTOCOL *
LoadedProgram (VOID)
{
  OPENABL_STORAGE_PROTOCOL *Program = NULL;

  if (EFI_ERROR (gBS->LocateProtocol (&mOpenAblStorageGuid, NULL,
                                      (VOID **)&Program))) {
    return NULL;
  }
  return Program;
}

BOOLEAN
OpenAblAndroidParam (IN UINTN Index, OUT CONST CHAR8 **Key,
                     OUT CONST CHAR8 **Value)
{
  /* Fetched once. Holds "key\0value\0" pairs and a final NUL, then zeros. */
  STATIC CHAR8   Params[256];
  STATIC BOOLEAN Asked;
  OPENABL_STORAGE_PROTOCOL *Storage;
  CONST CHAR8              *P;

  if (!Asked) {
    UINTN Size = sizeof (Params) - 2;

    Asked = TRUE;
    Storage = LoadedProgram ();
    if ((Storage == NULL) ||
        EFI_ERROR (Storage->AndroidParams (Storage, Params, &Size,
                                           StorageLog))) {
      Params[0] = '\0';
    }
  }
  for (P = Params; *P != '\0';) {
    CONST CHAR8 *K = P;
    CONST CHAR8 *V;

    P += AsciiStrLen (P) + 1;
    V = P;
    P += AsciiStrLen (P) + 1;
    if (Index-- == 0) {
      *Key = K;
      *Value = V;
      return TRUE;
    }
  }
  return FALSE;
}
