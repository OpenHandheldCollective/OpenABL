/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-source-exception */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/*
 * OpenAblMenuHost - protocols shared with the OpenABL program
 */

#ifndef __OPENABL_MENU_HOST_H__
#define __OPENABL_MENU_HOST_H__

#define OPENABL_MENU_HOST_PROTOCOL_GUID                                        \
  {                                                                            \
    0x6b3c9a52, 0x1f4e, 0x4d2a,                                                \
    {                                                                          \
      0x9c, 0x61, 0x2e, 0x7f, 0x0a, 0x5d, 0x83, 0xb4                           \
    }                                                                          \
  }

#define OPENABL_MENU_FLAG_EARLY BIT0
#define OPENABL_MENU_FLAG_MAIN  BIT1

/* State Flags. */
#define OPENABL_MENU_STATE_USB_HOST       BIT0
#define OPENABL_MENU_STATE_ALTERNATE_SLOT BIT1
#define OPENABL_MENU_STATE_VM_ENABLED     BIT2

#define OPENABL_MENU_ENTRY_FASTBOOT BIT0
#define OPENABL_MENU_ENTRY_RECOVERY BIT1

#define OPENABL_MENU_ACTION_START                 1
#define OPENABL_MENU_ACTION_RESTART_BOOTLOADER    2
#define OPENABL_MENU_ACTION_RECOVERY              3
#define OPENABL_MENU_ACTION_EMERGENCY             4
#define OPENABL_MENU_ACTION_POWER_OFF             5
#define OPENABL_MENU_ACTION_ALTERNATE_SLOT        6
#define OPENABL_MENU_ACTION_FASTBOOT_START        7
#define OPENABL_MENU_ACTION_FASTBOOT_STOP         8
#define OPENABL_MENU_ACTION_FASTBOOT_MENU         9
#define OPENABL_MENU_ACTION_BOOT_PREP             10
#define OPENABL_MENU_ACTION_BOOT_ANDROID          11
#define OPENABL_MENU_ACTION_BOOT_ANDROID_RECOVERY 12
/* Fastboot's USB gadget off and on, for USB host. */
#define OPENABL_MENU_ACTION_GADGET_PAUSE          13
#define OPENABL_MENU_ACTION_GADGET_RESUME         14
/* Verifies the image from SetBootImage for StartKernel. */
#define OPENABL_MENU_ACTION_BOOT_IMAGE_VERIFY     15
/* EFI_SUCCESS if the device powered on to charge. */
#define OPENABL_MENU_ACTION_CHARGER_BOOT          16
#define OPENABL_MENU_ACTION_CHARGER_SCREEN_ON     17
#define OPENABL_MENU_ACTION_CHARGER_SCREEN_OFF    18

typedef struct {
  UINT8  OffmodeCharging; /* Qualcomm's charger screen flag */
  UINT8  Flags;           /* OPENABL_MENU_STATE_* */
  UINT8  Entry;           /* OPENABL_MENU_ENTRY_* */
  UINT8  Reserved;
  UINT16 Key;             /* the key held at the start, a SCAN_* code */
  CHAR8  Version[32];     /* the ABL's version */
} OPENABL_MENU_STATE;

/* Laid out where BootLinux would put it; the devicetree may grow to 2 MiB.
 * KernelSize is how much of the Image to clean to the point of coherency. */
typedef struct {
  UINT64       KernelAddr;
  UINT64       KernelSize;
  UINT64       RamdiskAddr;
  UINT64       RamdiskSize;
  UINT64       DtbAddr;
  CONST CHAR8 *CmdLine;
} OPENABL_KERNEL;

typedef struct _OPENABL_MENU_HOST_PROTOCOL OPENABL_MENU_HOST_PROTOCOL;

typedef EFI_STATUS (EFIAPI *OPENABL_MENU_GET_STATE) (
    IN OPENABL_MENU_HOST_PROTOCOL *This, OUT OPENABL_MENU_STATE *State);

typedef EFI_STATUS (EFIAPI *OPENABL_MENU_DO_ACTION) (
    IN OPENABL_MENU_HOST_PROTOCOL *This, IN UINT32 Action);

/* Serves fastboot. Anything but EFI_SUCCESS tells the program to leave. */
typedef EFI_STATUS (EFIAPI *OPENABL_MENU_POLL) (
    IN OPENABL_MENU_HOST_PROTOCOL *This);

typedef VOID (EFIAPI *OPENABL_MENU_LOG) (
    IN OPENABL_MENU_HOST_PROTOCOL *This, IN CONST CHAR8 *Line);

/* Buffer is pool memory, the ABL's from then on. */
typedef EFI_STATUS (EFIAPI *OPENABL_MENU_SET_BOOT_IMAGE) (
    IN OPENABL_MENU_HOST_PROTOCOL *This, IN VOID *Buffer, IN UINTN Size);

/* Updates the devicetree and jumps to the kernel. Returns only on failure. */
typedef EFI_STATUS (EFIAPI *OPENABL_MENU_START_KERNEL) (
    IN OPENABL_MENU_HOST_PROTOCOL *This, IN CONST OPENABL_KERNEL *Kernel);

struct _OPENABL_MENU_HOST_PROTOCOL {
  UINT32                      Flags; /* OPENABL_MENU_FLAG_* */
  OPENABL_MENU_GET_STATE      GetState;
  OPENABL_MENU_DO_ACTION      DoAction;
  OPENABL_MENU_POLL           Poll;
  OPENABL_MENU_LOG            Log;
  OPENABL_MENU_SET_BOOT_IMAGE SetBootImage;
  OPENABL_MENU_START_KERNEL   StartKernel;
};

extern EFI_GUID gOpenAblMenuHostProtocolGuid;

#define OPENABL_STORAGE_PROTOCOL_GUID                                          \
  {                                                                            \
    0x9e1f4c27, 0x3b6d, 0x4a58,                                                \
    {                                                                          \
      0x8c, 0x0e, 0x7d, 0x2a, 0x5f, 0x13, 0xb6, 0xe9                           \
    }                                                                          \
  }

typedef struct _OPENABL_STORAGE_PROTOCOL OPENABL_STORAGE_PROTOCOL;

typedef VOID (EFIAPI *OPENABL_STORAGE_LOG) (IN CONST CHAR8 *Line);

typedef EFI_STATUS (EFIAPI *OPENABL_STORAGE_ANDROID_PARAMS) (
    IN OPENABL_STORAGE_PROTOCOL *This, OUT CHAR8 *Params OPTIONAL,
    IN OUT UINTN *Size, IN OPENABL_STORAGE_LOG Log OPTIONAL);

#define OPENABL_PREP_BOOT BIT0
typedef VOID (EFIAPI *OPENABL_STORAGE_BOOT_PREP) (
    IN OPENABL_STORAGE_PROTOCOL *This, IN UINT32 Flags,
    IN OPENABL_STORAGE_LOG Log OPTIONAL);

typedef EFI_STATUS (EFIAPI *OPENABL_STORAGE_RUN) (
    IN OPENABL_STORAGE_PROTOCOL *This);

typedef EFI_STATUS (EFIAPI *OPENABL_STORAGE_START_BOOT_IMAGE) (
    IN OPENABL_STORAGE_PROTOCOL *This, IN VOID *Buffer, IN UINTN Size,
    IN OPENABL_STORAGE_LOG Log);

typedef VOID (EFIAPI *OPENABL_STORAGE_LOG_SINK) (
    IN CONST UINT8 *Buffer, IN UINTN Size);

struct _OPENABL_STORAGE_PROTOCOL {
  OPENABL_STORAGE_ANDROID_PARAMS   AndroidParams;
  OPENABL_STORAGE_BOOT_PREP        BootPrep;
  OPENABL_STORAGE_RUN              Run;
  OPENABL_STORAGE_START_BOOT_IMAGE StartBootImage;
  OPENABL_STORAGE_LOG_SINK         LogSink;
};

#endif /* __OPENABL_MENU_HOST_H__ */
