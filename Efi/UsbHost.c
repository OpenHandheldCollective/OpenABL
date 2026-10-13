/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  UsbHost - USB host mode for booting from a USB drive on Qualcomm XBL
  firmware, and the kernel devicetree fix-up for a USB boot.
*/

#include "Efi/UsbHost.h"

#include "Efi/Efi.h"

#include <Guid/NonDiscoverableDevice.h>
#include <IndustryStandard/Acpi.h>
#include <Pi/PiFirmwareFile.h>
#include <Pi/PiFirmwareVolume.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/NonDiscoverableDevice.h>
#include <Protocol/Usb2HostController.h>
#include <Protocol/UsbIo.h>

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Core/DeviceTree.h"
#include "Core/EfiData.h"
#include "Core/UsbHostRegs.h"
#include "Efi/Files.h"
#include "Efi/Quirks.h"

// Defined in Efi/Efi.c; no header in the ABL's tree declares it.
extern EFI_GUID gQcomTokenSpaceGuid;

// e722b03f-b250-42ce-8ebd-5bd51812d037.
STATIC EFI_GUID mUsbConfigGuid = {
  0xe722b03f,
  0xb250,
  0x42ce,
  {0x8e, 0xbd, 0x5b, 0xd5, 0x18, 0x12, 0xd0, 0x37}};
// ae6ae96e-483f-42ae-9cc1-9fac1b584728.
STATIC EFI_GUID mPmicSchgGuid = {
  0xae6ae96e,
  0x483f,
  0x42ae,
  {0x9c, 0xc1, 0x9f, 0xac, 0x1b, 0x58, 0x47, 0x28}};
// b27ae8b1-3e10-4d07-ab5c-eb9a6dc6fa8f.
STATIC EFI_GUID mI2cGuid = {0xb27ae8b1,
                            0x3e10,
                            0x4d07,
                            {0xab, 0x5c, 0xeb, 0x9a, 0x6d, 0xc6, 0xfa, 0x8f}};
// PIL, as XBL's .dec has it (not the older header's GUID):
// 1f4f1da3-4d8d-4894-ba2a-13d3207f9739.
STATIC EFI_GUID mPilGuid = {0x1f4f1da3,
                            0x4d8d,
                            0x4894,
                            {0xba, 0x2a, 0x13, 0xd3, 0x20, 0x7f, 0x97, 0x39}};
// 7429eb07-f2e3-4a6b-aa96-c909cd956ef7.
STATIC EFI_GUID mPmicGlinkGuid = {
  0x7429eb07,
  0xf2e3,
  0x4a6b,
  {0xaa, 0x96, 0xc9, 0x09, 0xcd, 0x95, 0x6e, 0xf7}};
// Qualcomm's per-partition protocol whose interface is the type GUID:
// 6848de61-eb61-4def-9a8e-3817cbeb8f1c.
STATIC EFI_GUID mPartitionTypeGuid = {
  0x6848de61,
  0xeb61,
  0x4def,
  {0x9a, 0x8e, 0x38, 0x17, 0xcb, 0xeb, 0x8f, 0x1c}};
// ebd0a0a2-b9e5-4433-87c0-68b6b72699c7.
STATIC EFI_GUID mBasicDataGuid = {
  0xebd0a0a2,
  0xb9e5,
  0x4433,
  {0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7}};

// The edk2 drivers SM6115's ABL carries (FILE_GUIDs in its FV).
STATIC EFI_GUID mDrvNonDiscoverablePci = {
  0x71fd84cd,
  0x353b,
  0x464d,
  {0xb7, 0xa4, 0x6e, 0xa7, 0xb9, 0x69, 0x95, 0xcb}};
STATIC EFI_GUID mDrvXhci   = {0xb7f50e91,
                              0xa759,
                              0x412c,
                              {0xad, 0xe4, 0xdc, 0xd0, 0x3e, 0x7f, 0x7c, 0x28}};
STATIC EFI_GUID mDrvUsbBus = {0x240612b7,
                              0xa063,
                              0x11d4,
                              {0x9a, 0x3a, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};
STATIC EFI_GUID mDrvUsbMass = {
  0x9fb4b4a7,
  0x42c0,
  0x4bcd,
  {0x85, 0x40, 0x9b, 0xcc, 0x67, 0x11, 0xf8, 0x3e}};

// XBL's protocols, with only the slots used named. They have no header in
// the ABL's tree; the layouts follow LinuxLoader's.

typedef struct _USB_CONFIG USB_CONFIG;

// StartController and StopController.
typedef EFI_STATUS (EFIAPI *USB_CONFIG_CONTROLLER) (
  IN USB_CONFIG *This,
  IN UINT32      Core,
  IN UINT32      Mode
);

typedef struct _USB_CONFIG {
  UINT32 Revision;
  EFI_STATUS (EFIAPI *GetCoreBaseAddr) (
    IN struct _USB_CONFIG *This,
    IN UINT32              CoreType,
    OUT UINTN             *Base
  );
  EFI_STATUS (EFIAPI *ConfigUsb) (
    IN struct _USB_CONFIG *This,
    IN UINT32              Mode,
    IN UINT32              Core
  );
  UINTN ResetUsb;
  UINTN GetUsbfnConfig;
  UINTN GetSsUsbfnConfig;
  UINTN GetUsbfnConnStatus;
  EFI_STATUS (EFIAPI *GetUsbHostConfig) (
    IN struct _USB_CONFIG *This,
    IN UINT32              Mode,
    IN UINT32              Core,
    OUT UINT32            *CoreType
  );
  UINTN                 GetUsbMaxHostCoreNum;
  UINTN                 ExitUsbLibServices;
  USB_CONFIG_CONTROLLER StartController;
  USB_CONFIG_CONTROLLER StopController;
} USB_CONFIG;

// The VBUS slots, read at their offsets on the revisions that have them.
typedef EFI_STATUS (EFIAPI *VBUS_STATUS) (
  IN USB_CONFIG *This,
  IN UINT32      Core,
  OUT UINT32    *Status
);
typedef EFI_STATUS (EFIAPI *ENABLE_VBUS) (
  IN USB_CONFIG *This,
  IN UINT32      Core
);

// The PMIC SCHG protocol up to GetOtgStatus. GetChargerPmicIndex is read
// at its offset on the one revision that has it there.
typedef struct _SCHG {
  UINT64 Revision;
  UINTN  A[5];
  EFI_STATUS (EFIAPI *ConfigOtg) (
    IN UINT32 Pmic,
    IN UINT32 Config,
    IN UINT8  Value
  );
  EFI_STATUS (EFIAPI *SetOtgILimit) (
    IN UINT32 Pmic,
    IN UINT32 Milliamps
  );
  UINTN B[4];
  EFI_STATUS (EFIAPI *EnableOtg) (
    IN UINT32 Pmic,
    IN UINT8  On
  );
  UINTN C[2];
  EFI_STATUS (EFIAPI *GetOtgStatus) (
    IN UINT32   Pmic,
    OUT UINT32 *Status
  );
} SCHG;
#define SCHG_GET_CHARGER_PMIC_INDEX 0x190
typedef EFI_STATUS (EFIAPI *SCHG_PMIC_INDEX) (OUT UINT8 *Index);

typedef struct _I2C_SLAVE_CONFIG {
  UINT32 BusFrequencyKhz;
  UINT32 SlaveAddress;
  UINT32 Mode;
  UINT32 SlaveMaxClockStretchUs;
  UINT32 CoreConfiguration1;
  UINT32 CoreConfiguration2;
} I2C_SLAVE_CONFIG;

// Read and Write: Length bytes at register Offset, which is OffsetLength
// bytes wide. Done gets the bytes moved.
typedef UINT32 (EFIAPI *I2C_XFER) (
  IN VOID             *Handle,
  IN I2C_SLAVE_CONFIG *Config,
  IN UINT16            Offset,
  IN UINT16            OffsetLength,
  IN OUT UINT8        *Buffer,
  IN UINT16            Length,
  OUT UINT32          *Done,
  IN UINT32            TimeoutMs
);

// XBL's I2C protocol; its functions take no This.
typedef struct _I2C {
  UINT64 Revision;
  UINT32 (EFIAPI *Open) (
    IN UINT32  Instance,
    OUT VOID **Handle
  );
  I2C_XFER Read;
  I2C_XFER Write;
  UINTN    Transfer;
  UINT32 (EFIAPI *Close) (IN VOID *Handle);
} I2C;

typedef struct _PIL {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *ProcessPilImage) (IN CHAR16 *Name);
  UINTN ProcessPilImageExt;
  UINTN AuthElfFvImageFromBuffer;
  EFI_STATUS (EFIAPI *ProcessPilShutdown) (IN UINT32 Subsys);
} PIL;
// ProcessPilShutdown exists from this revision.
#define PIL_REVISION_V2 0x00010002ULL
#define ADSP_SUBSYS_ID  1u

typedef EFI_STATUS (EFIAPI *GLINK_CONNECT) (VOID);
typedef EFI_STATUS (EFIAPI *GLINK_LINK_STATUS) (OUT UINT8 *Open);
#define PMIC_GLINK_REVISION_V1 0x00010001ULL
// From this revision the USB-C buffers sit before Connect.
#define PMIC_GLINK_REVISION_VB 0x00010007ULL

// NON_DISCOVERABLE_DEVICE with its one MMIO resource, as edk2's
// RegisterNonDiscoverableMmioDevice builds it.
typedef struct _NON_DISCOVERABLE {
  // DMA is non-coherent; coherent DMA crashed.
  NON_DISCOVERABLE_DEVICE           Dev;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR Res;
  EFI_ACPI_END_TAG_DESCRIPTOR       End;
} NON_DISCOVERABLE;

// Its device path: a vendor node with the base, then the end.
typedef struct _NON_DISCOVERABLE_PATH {
  VENDOR_DEVICE_PATH       Vendor;
  UINT64                   Base;
  UINT8                    ResType;
  EFI_DEVICE_PATH_PROTOCOL End;
} __attribute__ ((packed)) NON_DISCOVERABLE_PATH;

// XBL's layouts, and edk2's for the structures RegisterXhci builds.
_Static_assert (
  offsetof (
    USB_CONFIG,
    GetUsbHostConfig
  ) == 0x38,
  "USB_CONFIG: XBL's layout"
);
_Static_assert (
  offsetof (
    USB_CONFIG,
    StartController
  ) == 0x50,
  "USB_CONFIG: XBL's layout"
);
_Static_assert (
  offsetof (
    USB_CONFIG,
    StopController
  ) == 0x58,
  "USB_CONFIG: XBL's layout"
);
_Static_assert (
  offsetof (
    SCHG,
    ConfigOtg
  ) == 0x30,
  "SCHG: XBL's layout"
);
_Static_assert (
  offsetof (
    SCHG,
    SetOtgILimit
  ) == 0x38,
  "SCHG: XBL's layout"
);
_Static_assert (
  offsetof (
    SCHG,
    EnableOtg
  ) == 0x60,
  "SCHG: XBL's layout"
);
_Static_assert (
  offsetof (
    SCHG,
    GetOtgStatus
  ) == 0x78,
  "SCHG: XBL's layout"
);
_Static_assert (
  offsetof (
    PIL,
    ProcessPilShutdown
  ) == 0x20,
  "PIL: XBL's layout"
);
_Static_assert (
  offsetof (
    I2C,
    Close
  ) == 0x28,
  "I2C: XBL's layout"
);
_Static_assert (
  sizeof (EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR) == 46,
  "EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR: an ACPI QWORD descriptor"
);
_Static_assert (
  sizeof (NON_DISCOVERABLE_PATH) == 33,
  "NON_DISCOVERABLE_PATH: edk2's layout"
);
_Static_assert (
  offsetof (
    NON_DISCOVERABLE,
    Res
  ) == 32,
  "NON_DISCOVERABLE: edk2's layout"
);

// State for the callbacks

STATIC BOOLEAN            mHaveBoard;
STATIC USB_HOST_BOARD     mBoard;
STATIC BOOLEAN            mUp;
STATIC BOOLEAN            mVbusOnByUs;
STATIC BOOLEAN            mAdspLoaded;
STATIC UINT64             mCoreBase;
STATIC EFI_HANDLE         mDevice;
STATIC USB_HOST_PROTOCOL *mProtocol;
// The ExitBootServices callback is registered.
STATIC BOOLEAN mAtExit;

// Logs "usb: " and the text to Log. The arguments are evaluated even when
// nothing is logged.
#define say(Log, ...) LogPrint ((Log), "usb: " __VA_ARGS__)

#define ST(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

STATIC VOID
StallMs (IN UINT32 Milliseconds)
{
  gBS->Stall ((UINTN)Milliseconds * 1000);
}

STATIC VOID
DelayUs (
  IN UINT32 Microseconds,
  IN VOID  *Context
)
{
  (VOID)Context;
  gBS->Stall (Microseconds);
}

// Gets the protocol on Handle through HandleProtocol. NULL if absent.
STATIC VOID *
OpenRaw (
  IN EFI_HANDLE Handle,
  IN EFI_GUID  *Guid
)
{
  VOID *Interface = NULL;

  if (gBS->HandleProtocol (Handle, Guid, &Interface) != EFI_SUCCESS) {
    return NULL;
  }
  return Interface;
}

STATIC UINTN
HandleCount (IN EFI_GUID *Guid)
{
  EFI_HANDLE *Handles;
  UINTN       Count = LocateHandles (Guid, &Handles);

  if (Handles) {
    gBS->FreePool (Handles);
  }
  return Count;
}

// ConnectController with an empty driver list (only its NULL end).
STATIC EFI_STATUS
Connect (
  IN EFI_HANDLE Handle,
  IN BOOLEAN    Recursive
)
{
  EFI_HANDLE None[1] = {NULL};

  return gBS->ConnectController (Handle, None, NULL, Recursive);
}

// Drops to TPL_APPLICATION, so the drivers' notify functions can run while
// waiting. Returns the TPL to restore.
STATIC EFI_TPL
LowerTpl (VOID)
{
  EFI_TPL Entry = gBS->RaiseTPL (TPL_HIGH_LEVEL);

  gBS->RestoreTPL (Entry);
  if (Entry > TPL_APPLICATION) {
    gBS->RestoreTPL (TPL_APPLICATION);
  }
  return Entry;
}

STATIC VOID
RestoreTpl (IN EFI_TPL Entry)
{
  if (Entry > TPL_APPLICATION) {
    gBS->RaiseTPL (Entry);
  }
}

// Whether a SimpleFileSystem is on a USB device.
STATIC BOOLEAN
UsbFilesystemPresent (VOID)
{
  EFI_HANDLE *Handles;
  UINTN   Count = LocateHandles (&gEfiSimpleFileSystemProtocolGuid, &Handles);
  BOOLEAN Found = FALSE;

  for (UINTN Index = 0; Index < Count && !Found; Index++) {
    OABL_BUFFER DevicePath = {0};
    UEFI_NODE   Node;
    UINTN       Offset = 0;

    if (!PathsHandlePath (Handles[Index], &DevicePath)) {
      continue;
    }
    while (!Found &&
           UefiNextNode (DevicePath.Data, DevicePath.Len, &Offset, &Node)) {
      // A messaging (3) USB (5) node.
      Found = Node.Type == 3 && Node.SubType == 5;
    }
    BufferFree (&DevicePath);
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  return Found;
}

// VBUS

// Sets the PMIC charger's OTG boost on PMIC PmicIndex or, without Given,
// on the charger PMIC that revision 0x10011 can name. TRUE when it went on.
STATIC BOOLEAN
SchgSetOtg (
  IN BOOLEAN  Given,
  IN UINT32   PmicIndex,
  IN BOOLEAN  On,
  IN LOG_SINK Log
)
{
  SCHG   *Schg = LocateProtocol (&mPmicSchgGuid);
  UINT64  Revision;
  BOOLEAN OtgOn = FALSE;

  if (!Schg) {
    say (Log, "SCHG not found");
    return FALSE;
  }
  // Only the slots this revision has are read.
  Revision = Schg->Revision;
  say (Log, "SCHG revision 0x%llx", (UINT64)Revision);
  if (Revision < USB_HOST_SCHG_REVISION_MIN) {
    say (Log, "SCHG too old: no charger writes");
    return FALSE;
  }
  if (Revision > USB_HOST_SCHG_REVISION_MAX_VERIFIED) {
    say (Log, "SCHG newer than any checked; using its append-only start");
  }
  if (!Given) {
    PmicIndex = 0;
    if (Revision == USB_HOST_SCHG_REVISION_DCDC_ENUMS) {
      SCHG_PMIC_INDEX GetIndex;
      CopyMem (
        &GetIndex, (UINT8 *)Schg + SCHG_GET_CHARGER_PMIC_INDEX,
        sizeof (GetIndex)
      );
      if (GetIndex) {
        UINT8      Charger = 0;
        EFI_STATUS Status  = GetIndex (&Charger);
        say (Log, "SCHG charger PMIC %u: %s", (UINT32)Charger, ST (Status));
        if (Status == EFI_SUCCESS) {
          PmicIndex = Charger;
        }
      }
    }
  }
  if (On) {
    if (Revision >= USB_HOST_SCHG_REVISION_DCDC_ENUMS && Schg->ConfigOtg) {
      say (
        Log, "SCHG ConfigOtg: %s",
        ST (Schg->ConfigOtg (PmicIndex, USB_HOST_SCHG_OTG_EN_SRC_CFG, 0))
      );
    }
    if (Schg->SetOtgILimit) {
      say (
        Log, "SCHG SetOtgILimit 500 mA: %s",
        ST (Schg->SetOtgILimit (PmicIndex, USB_HOST_SCHG_OTG_ILIMIT_MA))
      );
    }
  }
  if (Schg->EnableOtg) {
    EFI_STATUS Status = Schg->EnableOtg (PmicIndex, On);
    say (
      Log, "SCHG EnableOtg(%s, PMIC %u): %s", On ? "on" : "off",
      (UINT32)PmicIndex, ST (Status)
    );
    OtgOn = On && Status == EFI_SUCCESS;
  }
  if (On && Schg->GetOtgStatus) {
    UINT32     OtgStatus = 0;
    EFI_STATUS Status    = Schg->GetOtgStatus (PmicIndex, &OtgStatus);
    say (Log, "SCHG OTG status %u: %s", (UINT32)OtgStatus, ST (Status));
  }
  return OtgOn;
}

STATIC VOID
I2cClose (
  IN I2C  *I2c,
  IN VOID *Handle
)
{
  if (I2c->Close) {
    I2c->Close (Handle);
  }
}

// Sets the SGM41515 charger's OTG bit over I2C (Mangmi Air X: instance 6,
// address 0x1A, REG01 bit 5). TRUE when it reads back on.
STATIC BOOLEAN
ChargerSetOtg (
  IN BOOLEAN  On,
  IN LOG_SINK Log
)
{
  CONST UINT32     Instance = 6;
  CONST UINT16     Reg01    = 0x01;
  CONST UINT8      Otg      = 0x20;
  I2C             *I2c      = LocateProtocol (&mI2cGuid);
  I2C_SLAVE_CONFIG Config   = {
    .BusFrequencyKhz        = 400,
    .SlaveAddress           = 0x1A,
    .Mode                   = 0,
    .SlaveMaxClockStretchUs = 500,
    .CoreConfiguration1     = 0,
    .CoreConfiguration2     = 0,
  };
  VOID  *Handle = NULL;
  UINT32 Result;
  UINT32 Done  = 0;
  UINT8  Value = 0;
  UINT8  NewValue;

  if (!I2c) {
    say (Log, "I2C not found");
    return FALSE;
  }
  if (!I2c->Open || !I2c->Read || !I2c->Write) {
    return FALSE;
  }
  Result = I2c->Open (Instance, &Handle);
  if (Result != 0 || !Handle) {
    say (Log, "I2C open: %u", (UINT32)Result);
    return FALSE;
  }
  Result = I2c->Read (Handle, &Config, Reg01, 1, &Value, 1, &Done, 100);
  say (Log, "SGM41515 REG01 0x%x: %u", (UINT32)Value, (UINT32)Result);
  if (Result != 0) {
    I2cClose (I2c, Handle);
    return FALSE;
  }
  NewValue = On ? Value | Otg : Value & (UINT8)~Otg;
  Result   = I2c->Write (Handle, &Config, Reg01, 1, &NewValue, 1, &Done, 100);
  say (Log, "SGM41515 REG01 := 0x%x: %u", (UINT32)NewValue, (UINT32)Result);
  Value = 0;
  (VOID)I2c->Read (Handle, &Config, Reg01, 1, &Value, 1, &Done, 100);
  I2cClose (I2c, Handle);
  return On && (Value & Otg) != 0;
}

// Whether a volume would look like the ADSP's firmware partition to PIL,
// which finds it by its Basic Data type alone.
STATIC BOOLEAN
BasicData (IN EFI_HANDLE Handle)
{
  EFI_GUID *Type;

  if (OpenRaw (Handle, &mBasicDataGuid)) {
    return TRUE;
  }
  Type = OpenRaw (Handle, &mPartitionTypeGuid);
  return Type && CompareGuid (Type, &mBasicDataGuid);
}

STATIC BOOLEAN
HasAdspFirmware (IN EFI_HANDLE Handle)
{
  STATIC CHAR16                    Path[] = L"\\image\\adsp.mdt";
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs =
    GetProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid);
  EFI_FILE_PROTOCOL *Root;
  EFI_FILE_PROTOCOL *File;
  BOOLEAN            Ok;

  if (!Fs || Fs->OpenVolume (Fs, &Root) != EFI_SUCCESS) {
    return FALSE;
  }
  Ok = Root->Open (Root, &File, Path, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS;
  if (Ok) {
    File->Close (File);
  }
  Root->Close (Root);
  return Ok;
}

// The most volumes ParkOtherBasicData parks.
#define PARK_MAX 8

// PIL fails with LOAD_ERROR unless only one Basic Data FAT volume is
// mounted, and a Linux card made with the Basic Data type collides with
// modem_a. Disconnects every such volume but the one with the ADSP firmware
// and returns how many it parked in Parked.
STATIC UINTN
ParkOtherBasicData (
  IN LOG_SINK    Log,
  OUT EFI_HANDLE Parked[PARK_MAX]
)
{
  EFI_HANDLE *Handles;
  EFI_HANDLE  Keep = NULL;
  UINTN   Count = LocateHandles (&gEfiSimpleFileSystemProtocolGuid, &Handles);
  UINTN   Candidates  = 0;
  UINTN   ParkedCount = 0;
  UINTN   Tried       = 0;
  BOOLEAN HaveKeep    = FALSE;

  if (!Count) {
    return 0;
  }
  // Move the candidates to the start of Handles.
  for (UINTN Index = 0; Index < Count; Index++) {
    if (BasicData (Handles[Index])) {
      Handles[Candidates++] = Handles[Index];
    }
  }
  for (UINTN Index = 0; Index < Candidates && !HaveKeep; Index++) {
    if (HasAdspFirmware (Handles[Index])) {
      Keep     = Handles[Index];
      HaveKeep = TRUE;
    }
  }
  if (Candidates > 1 && HaveKeep) {
    for (UINTN Index = 0; Index < Candidates && Tried < PARK_MAX; Index++) {
      if (Handles[Index] == Keep) {
        continue;
      }
      Tried++;
      if (gBS->DisconnectController (Handles[Index], NULL, NULL) ==
          EFI_SUCCESS) {
        Parked[ParkedCount++] = Handles[Index];
      }
    }
  }
  gBS->FreePool (Handles);
  say (
    Log, "ADSP: Basic Data volumes %llu%s, parked %llu", (UINT64)Candidates,
    HaveKeep ? ", firmware found" : ", firmware MISSING", (UINT64)ParkedCount
  );
  return ParkedCount;
}

STATIC BOOLEAN
AdspLoad (IN LOG_SINK Log)
{
  // The plain "ADSP" name fails; FULL_ADSP is the client.
  CHAR16     Name[] = L"FULL_ADSP";
  PIL       *Pil    = LocateProtocol (&mPilGuid);
  EFI_HANDLE Parked[PARK_MAX];
  UINTN      ParkedCount;
  EFI_STATUS Status;

  if (!Pil) {
    say (Log, "PIL not found: no ADSP");
    return FALSE;
  }
  say (Log, "PIL revision 0x%llx", (UINT64)Pil->Revision);
  if (!Pil->ProcessPilImage) {
    return FALSE;
  }
  ParkedCount = ParkOtherBasicData (Log, Parked);
  Status      = Pil->ProcessPilImage (Name);
  for (UINTN Index = 0; Index < ParkedCount; Index++) {
    (VOID)Connect (Parked[Index], TRUE);
  }
  say (Log, "ADSP load: %s", ST (Status));
  return Status == EFI_SUCCESS;
}

// Connects the ADSP's PMIC glink channel, which carries the VBUS request,
// and logs its state.
STATIC VOID
GlinkConfirm (IN LOG_SINK Log)
{
  UINT8            *Glink = LocateProtocol (&mPmicGlinkGuid);
  GLINK_CONNECT     ConnectFn;
  GLINK_LINK_STATUS LinkStatusFn;
  UINT64            Revision;
  UINTN             ConnectAt;
  UINTN             LinkStatusAt;

  if (!Glink) {
    say (Log, "PMIC glink not found");
    return;
  }
  // Slot offsets depend on the revision, as LinuxLoader reads them.
  CopyMem (&Revision, Glink, sizeof (Revision));
  if (Revision >= PMIC_GLINK_REVISION_VB) {
    ConnectAt    = 0x40;
    LinkStatusAt = 0x48;
  } else if (Revision == PMIC_GLINK_REVISION_V1) {
    ConnectAt    = 0x30;
    LinkStatusAt = 0x38;
  } else {
    say (Log, "PMIC glink revision 0x%llx: layout unknown", (UINT64)Revision);
    return;
  }
  CopyMem (&ConnectFn, Glink + ConnectAt, sizeof (ConnectFn));
  if (ConnectFn) {
    (VOID)ConnectFn ();
  }
  CopyMem (&LinkStatusFn, Glink + LinkStatusAt, sizeof (LinkStatusFn));
  if (LinkStatusFn) {
    UINT8 Open = 0;
    (VOID)LinkStatusFn (&Open);
    say (
      Log, "PMIC glink 0x%llx: channel %s", (UINT64)Revision,
      Open ? "open" : "not open"
    );
  }
}

STATIC VOID
AdspShutdown (VOID)
{
  PIL *Pil;

  if (!mAdspLoaded) {
    return;
  }
  mAdspLoaded = FALSE;
  Pil         = LocateProtocol (&mPilGuid);
  if (!Pil) {
    return;
  }
  // The slot exists from revision 2.
  if (Pil->Revision >= PIL_REVISION_V2 && Pil->ProcessPilShutdown) {
    (VOID)Pil->ProcessPilShutdown (ADSP_SUBSYS_ID);
  }
}

STATIC BOOLEAN
BoardVbus (
  IN BOOLEAN  On,
  IN LOG_SINK Log
)
{
  if (!mHaveBoard) {
    return FALSE;
  }
  switch (mBoard) {
  case USB_HOST_BOARD_SM8250:
    return SchgSetOtg (TRUE, USB_HOST_SCHG_PMIC_INDEX_8250, On, Log);
  case USB_HOST_BOARD_SM6115:
    if (On) {
      SchgSetOtg (FALSE, 0, TRUE, Log);
      return ChargerSetOtg (TRUE, Log);
    }
    ChargerSetOtg (FALSE, Log);
    SchgSetOtg (FALSE, 0, FALSE, Log);
    return FALSE;
  case USB_HOST_BOARD_GLINK:
    if (!On || mAdspLoaded) {
      return TRUE;
    }
    if (AdspLoad (Log)) {
      mAdspLoaded = TRUE;
      GlinkConfirm (Log);
      return TRUE;
    }
    return FALSE;
  }
  return FALSE;
}

// The bundled edk2 USB stack (SM6115)

// Loads and starts a driver from any firmware volume (the ABL's). Returns
// its image handle, or NULL.
STATIC EFI_HANDLE
StartFvDriver (
  IN EFI_GUID    *File,
  IN CONST CHAR8 *Name,
  IN LOG_SINK     Log
)
{
  EFI_HANDLE *Volumes;
  UINTN Count = LocateHandles (&gEfiFirmwareVolume2ProtocolGuid, &Volumes);

  if (!Count) {
    say (Log, "no firmware volume for %s", Name);
    return NULL;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_FIRMWARE_VOLUME2_PROTOCOL *Volume =
      OpenRaw (Volumes[Index], &gEfiFirmwareVolume2ProtocolGuid);
    VOID      *Buffer               = NULL;
    UINTN      Size                 = 0;
    UINT32     AuthenticationStatus = 0;
    EFI_HANDLE Image                = NULL;
    EFI_STATUS Status;

    if (!Volume) {
      continue;
    }
    Status = Volume->ReadSection (
      Volume, File, EFI_SECTION_PE32, 0, &Buffer, &Size, &AuthenticationStatus
    );
    if (Status != EFI_SUCCESS || !Buffer) {
      continue;
    }
    Status = gBS->LoadImage (FALSE, gImageHandle, NULL, Buffer, Size, &Image);
    gBS->FreePool (Buffer);
    if (Status == EFI_SUCCESS) {
      Status = StartImageFreeExitData (Image);
    }
    say (Log, "start %s: %s", Name, ST (Status));
    gBS->FreePool (Volumes);
    return Status == EFI_SUCCESS ? Image : NULL;
  }
  gBS->FreePool (Volumes);
  say (Log, "%s isn't in any firmware volume", Name);
  return NULL;
}

// edk2's RegisterNonDiscoverableMmioDevice for one XHCI window: installs
// both interfaces on a new handle, or neither. They are never freed, so
// they live as long as the handle.
STATIC EFI_STATUS
RegisterXhci (
  IN UINT64       Base,
  OUT EFI_HANDLE *Out
)
{
  NON_DISCOVERABLE      *Device = AllocateZeroPool (sizeof (*Device));
  NON_DISCOVERABLE_PATH *Path   = AllocateZeroPool (sizeof (*Path));
  EFI_HANDLE             Handle = NULL;
  EFI_STATUS             Status;

  if (!Device || !Path) {
    FreePool (Device);
    FreePool (Path);
    return EFI_OUT_OF_RESOURCES;
  }
  Device->Dev.Type         = &gEdkiiNonDiscoverableXhciDeviceGuid;
  Device->Dev.DmaType      = NonDiscoverableDeviceDmaTypeNonCoherent;
  Device->Dev.Initialize   = NULL;
  Device->Dev.Resources    = &Device->Res;
  Device->Res.Desc         = ACPI_ADDRESS_SPACE_DESCRIPTOR;
  Device->Res.Len          = (UINT16)(sizeof (Device->Res) - 3);
  Device->Res.ResType      = ACPI_ADDRESS_SPACE_TYPE_MEM;
  Device->Res.GenFlag      = 0;
  Device->Res.SpecificFlag = 0;
  Device->Res.AddrSpaceGranularity =
    Base + USB_HOST_XHCI_WINDOW > (1ULL << 32) ? 64 : 32;
  Device->Res.AddrRangeMin          = Base;
  Device->Res.AddrRangeMax          = Base + USB_HOST_XHCI_WINDOW - 1;
  Device->Res.AddrTranslationOffset = 0;
  Device->Res.AddrLen               = USB_HOST_XHCI_WINDOW;
  Device->End.Desc                  = ACPI_END_TAG_DESCRIPTOR;
  Device->End.Checksum              = 0;

  Path->Vendor.Header.Type      = HARDWARE_DEVICE_PATH;
  Path->Vendor.Header.SubType   = HW_VENDOR_DP;
  Path->Vendor.Header.Length[0] = (UINT8)(sizeof (*Path) - sizeof (Path->End));
  Path->Vendor.Header.Length[1] = 0;
  Path->Vendor.Guid             = gEdkiiNonDiscoverableDeviceProtocolGuid;
  Path->Base                    = Base;
  Path->ResType                 = 0;
  Path->End.Type                = END_DEVICE_PATH_TYPE;
  Path->End.SubType             = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  Path->End.Length[0]           = sizeof (Path->End);
  Path->End.Length[1]           = 0;

  Status = gBS->InstallProtocolInterface (
    &Handle, &gEdkiiNonDiscoverableDeviceProtocolGuid, EFI_NATIVE_INTERFACE,
    Device
  );
  if (Status != EFI_SUCCESS) {
    FreePool (Device);
    FreePool (Path);
    return Status;
  }
  Status = gBS->InstallProtocolInterface (
    &Handle, &gEfiDevicePathProtocolGuid, EFI_NATIVE_INTERFACE, Path
  );
  if (Status != EFI_SUCCESS) {
    // The bus drivers can't use the device without a path, so remove it, and
    // with it the handle. A driver that already holds it keeps the memory.
    if (gBS->UninstallProtocolInterface (
          Handle, &gEdkiiNonDiscoverableDeviceProtocolGuid, Device
        ) == EFI_SUCCESS) {
      FreePool (Device);
    }
    FreePool (Path);
    return Status;
  }
  *Out = Handle;
  return EFI_SUCCESS;
}

// Bring-up

// Sets EnableSLTLoopback to 1 (the host-attach unlock), or deletes it.
STATIC VOID
SetSltLoopback (
  IN BOOLEAN  On,
  IN LOG_SINK Log
)
{
  STATIC CHAR16 Name[] = L"EnableSLTLoopback";
  UINT8         One    = 1;
  EFI_STATUS    Status;

  if (On) {
    Status = gRT->SetVariable (
      Name, &gQcomTokenSpaceGuid, EFI_VARIABLE_BOOTSERVICE_ACCESS, 1, &One
    );
  } else {
    // Delete: no attributes and no data, but a non-NULL data pointer.
    Status = gRT->SetVariable (Name, &gQcomTokenSpaceGuid, 0, 0, &One);
  }
  say (
    Log, "%s%s",
    On ? "EnableSLTLoopback = 1 (host attach unlock): "
       : "EnableSLTLoopback deleted: ",
    ST (Status)
  );
}

// Returns to device mode: VBUS off, then, with UsbConfig, the host
// controller stopped and the device one started.
STATIC VOID
RevertToDevice (
  IN USB_CONFIG *Config OPTIONAL,
  IN LOG_SINK    Log
)
{
  // The core's registers, mapped by XBL.
  if (mHaveBoard && UsbHostBoardBundledStack (mBoard) && mCoreBase != 0) {
    UsbHostClearVbusValid (&gMmioHardware, mCoreBase);
  }
  BoardVbus (FALSE, Log);
  SetSltLoopback (FALSE, Log);
  if (!Config) {
    return;
  }
  if (Config->StopController) {
    say (
      Log, "StopController(host): %s",
      ST (Config->StopController (
        Config, USB_HOST_USB_CONFIG_CORE_0, USB_HOST_USB_CONFIG_HOST_MODE_XHCI
      ))
    );
  }
  if (Config->StartController) {
    say (
      Log, "StartController(device SS): %s",
      ST (Config->StartController (
        Config, USB_HOST_USB_CONFIG_CORE_0, USB_HOST_USB_CONFIG_DEVICE_MODE_SS
      ))
    );
  }
}

STATIC VOID EFIAPI
AtExitBootServices (
  IN EFI_EVENT Event,
  IN VOID     *Context
)
{
  (VOID)Event;
  (VOID)Context;
  AdspShutdown ();
  if (mVbusOnByUs) {
    BoardVbus (FALSE, LOG_QUIET);
  }
}

STATIC VOID
SetVbusValidAfterConnect (
  IN CONST MMIO *Io,
  IN UINT64      Base,
  IN LOG_SINK    Log
)
{
  UINT32 UsbCmd = MmioRead32 (Io, Base + USB_HOST_DWC3_XHCI_OP);
  UINT32 UsbSts = MmioRead32 (Io, Base + USB_HOST_DWC3_XHCI_OP + 4);

  say (Log, "USBCMD 0x%x USBSTS 0x%x", (UINT32)UsbCmd, (UINT32)UsbSts);
  // The XHCI driver's reset clears these bits.
  UsbHostSetVbusValid (Io, Base);
}

// Waits for a USB device beyond the Before already present, then for a
// filesystem on it. TRUE when a device came.
STATIC BOOLEAN
WaitForDrive (
  IN CONST MMIO *Io,
  IN UINT64      Base,
  IN UINTN       Before,
  IN LOG_SINK    Log
)
{
  EFI_TPL Entry     = LowerTpl ();
  BOOLEAN Found     = FALSE;
  UINT32  ElapsedMs = 0;

  while (ElapsedMs < USB_HOST_WAIT_TOTAL) {
    USB_HOST_PORTSC Ports;
    UINTN           Now;
    UINTN           UsbIo;

    if (ElapsedMs > 0) {
      StallMs (10);
    }
    if (ElapsedMs % USB_HOST_WAIT_CONNECT_EVERY == 0) {
      InventoryConnectAll ();
    }
    Now   = HandleCount (&gEfiUsbIoProtocolGuid);
    UsbIo = Now > Before ? Now - Before : 0;
    Ports = UsbHostPorts (Io, Base);
    if (ElapsedMs % 500 == 0) {
      say (
        Log, "t=%ums Usb2Hc=%llu PORTSC 0x%x 0x%x UsbIo=%llu",
        (UINT32)ElapsedMs, (UINT64)HandleCount (&gEfiUsb2HcProtocolGuid),
        (UINT32)Ports.Portsc1, (UINT32)Ports.Portsc2, (UINT64)UsbIo
      );
    }
    if (UsbIo == 0 && ElapsedMs >= USB_HOST_WAIT_NO_CONNECT &&
        !UsbHostConnected (Ports)) {
      say (Log, "nothing plugged in after %u ms", USB_HOST_WAIT_NO_CONNECT);
      break;
    }
    if (UsbIo > 0) {
      UINT32 WaitedMs = 0;
      while (WaitedMs < USB_HOST_WAIT_FILESYSTEM) {
        if (WaitedMs % USB_HOST_WAIT_CONNECT_EVERY == 0) {
          InventoryConnectAll ();
        }
        if (UsbFilesystemPresent ()) {
          say (Log, "a USB filesystem after %u ms", (UINT32)WaitedMs);
          break;
        }
        StallMs (2);
        WaitedMs += 2;
      }
      if (WaitedMs >= USB_HOST_WAIT_FILESYSTEM) {
        say (
          Log, "no USB filesystem after %u ms; going on",
          USB_HOST_WAIT_FILESYSTEM
        );
      }
      Found = TRUE;
      break;
    }
    ElapsedMs += 10;
  }
  RestoreTpl (Entry);
  return Found;
}

// Starts the edk2 host stack on the core (SM6115). After a teardown the
// drivers and the device still exist and are only connected again.
STATIC BOOLEAN
StartBundled (
  IN UINT64   Base,
  IN LOG_SINK Log
)
{
  EFI_HANDLE Pci;
  EFI_HANDLE Xhci;
  EFI_HANDLE Device = NULL;
  EFI_HANDLE Drivers[3];
  UINTN      DriverCount = 0;
  EFI_STATUS Status;

  if (mDevice) {
    Status = Connect (mDevice, TRUE);
    say (Log, "connect the XHCI device again: %s", ST (Status));
    return TRUE;
  }
  Pci =
    StartFvDriver (&mDrvNonDiscoverablePci, "NonDiscoverablePciDeviceDxe", Log);
  Xhci   = StartFvDriver (&mDrvXhci, "XhciDxe", Log);
  Status = RegisterXhci (Base, &Device);
  if (Status != EFI_SUCCESS) {
    say (Log, "registering the XHCI device: %s", ST (Status));
    return FALSE;
  }
  mDevice = Device;
  if (Pci) {
    Drivers[DriverCount++] = Pci;
  }
  if (Xhci) {
    Drivers[DriverCount++] = Xhci;
  }
  Drivers[DriverCount] = NULL;
  say (
    Log, "connect PCI + XHCI: %s",
    ST (gBS->ConnectController (Device, Drivers, NULL, FALSE))
  );
  StartFvDriver (&mDrvUsbBus, "UsbBusDxe", Log);
  StartFvDriver (&mDrvUsbMass, "UsbMassStorageDxe", Log);
  (VOID)Connect (Device, TRUE);
  return TRUE;
}

// The bring-up on Board: the core into host mode, VBUS, the stack, then
// the wait for a drive. Returns to device mode without one.
STATIC BOOLEAN
Run (
  IN USB_HOST_BOARD Board,
  IN LOG_SINK       Log
)
{
  USB_CONFIG *Config = LocateProtocol (&mUsbConfigGuid);
  // The core's registers, mapped by XBL: its window (from UsbConfig or the
  // SoC's known base) and the EUD register.
  CONST MMIO *Io      = &gMmioHardware;
  BOOLEAN     Bundled = UsbHostBoardBundledStack (Board);
  BOOLEAN     Unlock;
  BOOLEAN     Vbus = FALSE;
  BOOLEAN     Found;
  UINT32      Revision;
  UINT32      CoreType = USB_HOST_USB_CONFIG_CORE_MAX;
  UINTN       GotBase  = 0;
  UINTN       Before;
  UINTN       StatusAt;
  UINTN       EnableAt;
  UINT64      Base;
  UINT64      EudAt;

  if (!Config) {
    say (Log, "UsbConfig not found");
    return FALSE;
  }
  // The VBUS slots are read only on revisions that have them.
  Revision = Config->Revision;
  say (Log, "UsbConfig revision 0x%x", (UINT32)Revision);
  if (Config->GetUsbHostConfig) {
    (VOID)Config->GetUsbHostConfig (
      Config, USB_HOST_USB_CONFIG_HOST_MODE_XHCI, USB_HOST_USB_CONFIG_CORE_0,
      &CoreType
    );
  }
  if (Config->GetCoreBaseAddr && CoreType < USB_HOST_USB_CONFIG_CORE_MAX) {
    (VOID)Config->GetCoreBaseAddr (Config, CoreType, &GotBase);
  }
  Base = GotBase == 0 ? UsbHostBoardXhciBaseFallback (Board) : (UINT64)GotBase;
  mCoreBase = Base;
  say (Log, "host core %u, base 0x%llx", (UINT32)CoreType, (UINT64)Base);
  // USB devices present before host mode, such as a VM's keyboard, aren't
  // the drive.
  Before = HandleCount (&gEfiUsbIoProtocolGuid);

  BoardVbus (TRUE, Log);
  mVbusOnByUs = TRUE;
  StallMs (200);

  if (Config->StopController) {
    (VOID)Config->StopController (
      Config, USB_HOST_USB_CONFIG_CORE_0, USB_HOST_USB_CONFIG_DEVICE_MODE
    );
  }
  if (!Bundled) {
    UINT32 Dctl;
    UINT32 After;
    if (UsbHostClearRunStop (Io, Base, DelayUs, NULL, &Dctl, &After)) {
      say (
        Log, "DCTL 0x%x: RUN_STOP cleared, now 0x%x", (UINT32)Dctl,
        (UINT32)After
      );
    } else {
      say (Log, "DCTL 0x%x", (UINT32)Dctl);
    }
  }
  if (Config->ConfigUsb) {
    say (
      Log, "ConfigUsb(host): %s",
      ST (Config->ConfigUsb (
        Config, USB_HOST_USB_CONFIG_HOST_MODE_XHCI, USB_HOST_USB_CONFIG_CORE_0
      ))
    );
  }
  if (UsbHostBoardEudEnable (Board, &EudAt)) {
    UINT32  Eud;
    BOOLEAN Cleared = UsbHostDisableEud (Io, EudAt, DelayUs, NULL, &Eud);
    say (Log, "EUD 0x%x%s", (UINT32)Eud, Cleared ? ": turned off" : "");
  }
  if (!Config->StartController) {
    say (Log, "no StartController");
    return FALSE;
  }
  Unlock = Revision >= USB_HOST_USB_CONFIG_REVISION_SLT_UNLOCK;
  if (Unlock) {
    SetSltLoopback (TRUE, Log);
  }
  say (
    Log, "StartController(host): %s",
    ST (Config->StartController (
      Config, USB_HOST_USB_CONFIG_CORE_0, USB_HOST_USB_CONFIG_HOST_MODE_XHCI
    ))
  );
  if (Unlock) {
    SetSltLoopback (FALSE, Log);
  }
  if (Bundled) {
    UINT32 Gctl;
    UsbHostSetVbusValid (Io, Base);
    Gctl = UsbHostForceHost (Io, Base, DelayUs, NULL);
    say (
      Log, "forced host: GCTL 0x%x -> 0x%x", (UINT32)Gctl,
      (UINT32)MmioRead32 (Io, Base + USB_HOST_DWC3_GCTL)
    );
  }

  if (UsbHostUsbConfigVbusSlots (Revision, &StatusAt, &EnableAt)) {
    ENABLE_VBUS EnableVbus;
    VBUS_STATUS VbusStatus;
    CopyMem (&EnableVbus, (UINT8 *)Config + EnableAt, sizeof (EnableVbus));
    if (EnableVbus) {
      EFI_STATUS Status = EnableVbus (Config, USB_HOST_USB_CONFIG_CORE_0);
      say (Log, "UsbEnableVbus: %s", ST (Status));
      Vbus = Status == EFI_SUCCESS;
    }
    CopyMem (&VbusStatus, (UINT8 *)Config + StatusAt, sizeof (VbusStatus));
    if (VbusStatus) {
      UINT32     Value = MAX_UINT32;
      EFI_STATUS Status =
        VbusStatus (Config, USB_HOST_USB_CONFIG_CORE_0, &Value);
      say (Log, "GetUsbVbusStatus %u: %s", (UINT32)Value, ST (Status));
    }
  }
  if (!Vbus) {
    say (Log, "VBUS from the board");
    BoardVbus (TRUE, Log);
  }

  if (Bundled && !StartBundled (Base, Log)) {
    return FALSE;
  }
  say (Log, "Usb2Hc: %llu", (UINT64)HandleCount (&gEfiUsb2HcProtocolGuid));
  if (Bundled) {
    SetVbusValidAfterConnect (Io, Base, Log);
  }
  Found = WaitForDrive (Io, Base, Before, Log);
  if (!Found) {
    RevertToDevice (Config, Log);
  }
  return Found;
}

USB_HOST_OUTCOME
UsbHostBringUp (
  IN USB_HOST_BOARD Board,
  IN LOG_SINK       Log
)
{
  BOOLEAN NowUp;
  BOOLEAN Resident;

  if (mUp) {
    say (Log, "host mode is up already");
    return (USB_HOST_OUTCOME){.Up = TRUE, .Resident = TRUE};
  }
  mBoard     = Board;
  mHaveBoard = TRUE;
  say (Log, "host bring-up on %s", UsbHostBoardName (Board));
  NowUp = Run (Board, Log);
  mUp   = NowUp;
  if (!NowUp) {
    BoardVbus (FALSE, Log);
  }
  Resident = NowUp || mVbusOnByUs || mAdspLoaded;
  if (Resident && !mAtExit) {
    EFI_EVENT Event;
    mAtExit = TRUE;
    // The callback is in this image, which stays loaded.
    say (
      Log, "ExitBootServices release: %s",
      ST (gBS->CreateEvent (
        EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_CALLBACK, AtExitBootServices, NULL,
        &Event
      ))
    );
  }
  say (
    Log, "host bring-up %s",
    NowUp ? "done: a USB drive is up" : "found no drive"
  );
  return (USB_HOST_OUTCOME){.Up = NowUp, .Resident = Resident};
}

// After the bring-up

STATIC VOID
PublishFlags (VOID)
{
  if (mProtocol) {
    mProtocol->Flags = mUp ? USB_HOST_FLAG_UP : 0;
  }
}

VOID
UsbHostTeardown (VOID)
{
  if (!mUp) {
    return;
  }
  mUp = FALSE;
  // Registered by StartBundled.
  if (mDevice) {
    (VOID)gBS->DisconnectController (mDevice, NULL, NULL);
  }
  RevertToDevice (LocateProtocol (&mUsbConfigGuid), LOG_QUIET);
  mVbusOnByUs = FALSE;
  PublishFlags ();
}

VOID
UsbHostReleaseVbus (VOID)
{
  if (!mVbusOnByUs) {
    return;
  }
  mVbusOnByUs = FALSE;
  BoardVbus (FALSE, LOG_QUIET);
  mUp = FALSE;
  PublishFlags ();
}

BOOLEAN
UsbHostIsUp (VOID)
{
  return mUp;
}

VOID
UsbHostOsHandoff (VOID)
{
  AdspShutdown ();
}

STATIC VOID EFIAPI
ProtocolTeardown (IN USB_HOST_PROTOCOL *This)
{
  (VOID)This;
  UsbHostTeardown ();
}

STATIC VOID EFIAPI
ProtocolReleaseVbus (IN USB_HOST_PROTOCOL *This)
{
  (VOID)This;
  UsbHostReleaseVbus ();
}

STATIC VOID EFIAPI
ProtocolOsHandoff (IN USB_HOST_PROTOCOL *This)
{
  (VOID)This;
  UsbHostOsHandoff ();
}

STATIC EFI_STATUS EFIAPI
ProtocolBringUp (
  IN USB_HOST_PROTOCOL  *This,
  IN OPENABL_STORAGE_LOG AblLog OPTIONAL
)
{
  LOG_SINK Log;
  UINT32   Chip;
  BOOLEAN  NowUp;

  (VOID)This;
  if (!mHaveBoard) {
    return EFI_NOT_READY;
  }
  Log = QuirksAblLog (AblLog);
  // As for the first bring-up: the GPT quirk before a drive's partitions
  // are read, the FAT quirk on what it mounts.
  Chip = QuirksChipId ();
  QuirksGptClobber (Chip, Log);
  NowUp = UsbHostBringUp (mBoard, Log).Up;
  QuirksFatCache (Chip, Log);
  PublishFlags ();
  return NowUp ? EFI_SUCCESS : EFI_NOT_FOUND;
}

EFI_STATUS
UsbHostInstallProtocol (VOID)
{
  STATIC EFI_GUID    Guid = USB_HOST_PROTOCOL_GUID;
  USB_HOST_PROTOCOL *Protocol;
  EFI_HANDLE         Handle = NULL;

  if (mProtocol) {
    PublishFlags ();
    return EFI_SUCCESS;
  }
  Protocol = AllocatePool (sizeof (*Protocol));
  if (!Protocol) {
    return EFI_OUT_OF_RESOURCES;
  }
  *Protocol = (USB_HOST_PROTOCOL){
    .Revision    = USB_HOST_PROTOCOL_REVISION,
    .Flags       = 0,
    .Teardown    = ProtocolTeardown,
    .ReleaseVbus = ProtocolReleaseVbus,
    .OsHandoff   = ProtocolOsHandoff,
    .BringUp     = ProtocolBringUp,
  };
  mProtocol = Protocol;
  PublishFlags ();
  // Never freed, so it outlives the handle.
  return gBS->InstallProtocolInterface (
    &Handle, &Guid, EFI_NATIVE_INTERFACE, Protocol
  );
}

// USB boot devicetree fix-up

VOID
UsbBootFixDeviceTree (
  IN OUT UINT8 *Fdt,
  IN UINTN      Capacity,
  IN UINT32     Chip,
  IN LOG_SINK   Log
)
{
  STATIC CONST CHAR8 *CONST Dwc3[] = {"snps,dwc3", "qcom,snps-dwc3"};
  USB_HOST_BOARD            Board;

  if (UsbHostBoardOf (Chip, "", &Board) && Board == USB_HOST_BOARD_SM6115) {
    return;
  }
  // "host" and its NUL.
  switch (
    DtSetPropByCompatible (Fdt, Capacity, Dwc3, 2, "dr_mode", "host", 5)) {
  case DT_OK:
    LogLine (Log, "devicetree: dwc3 dr_mode=host, for the USB boot");
    break;
  case DT_NOT_FOUND:
    LogLine (Log, "devicetree: no dwc3 node - host-mode patch skipped");
    break;
  case DT_NO_SPACE:
    LogLine (Log, "devicetree: no room for dwc3 dr_mode=host");
    break;
  case DT_BAD_BLOB:
    LogLine (Log, "devicetree: not one to fix up");
    break;
  }
}
