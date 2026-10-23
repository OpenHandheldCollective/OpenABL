/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  AndroidImage - Android partition formats.
*/

#include "Core/AndroidImage.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/DeviceTree.h"

STATIC CONST UINT8 mFdtMagic[4] = {0xd0, 0x0d, 0xfe, 0xed};

STATIC BOOLEAN
Same (
  IN CONST UINT8 *A,
  IN CONST VOID  *B,
  IN UINTN        Size
)
{
  return TextEq ((OABL_TEXT){(CONST CHAR8 *)A, Size}, (OABL_TEXT){B, Size});
}

// Copies a header string field up to the first NUL. Invalid UTF-8 becomes
// U+FFFD.
STATIC CHAR8 *
Cstr (
  IN CONST UINT8 *Field,
  IN UINTN        Size
)
{
  UINTN End = 0;

  while (End < Size && Field[End]) {
    End++;
  }
  return Utf8Lossy (Field, End);
}

STATIC UINT64
Align (
  IN UINT64 Value,
  IN UINT64 Alignment
)
{
  return (Value + Alignment - 1) / Alignment * Alignment;
}

BOOLEAN
AndroidBootImageParse (
  IN CONST UINT8         *Header,
  IN UINTN                Size,
  OUT ANDROID_BOOT_IMAGE *Out
)
{
  ANDROID_BOOT_IMAGE Image = {0};
  UINT32             KernelSize;
  UINT32             RamdiskSize;
  UINT32             SecondSize = 0;
  UINT32             Page;
  UINT32             Os;
  UINT32             Rdtbo = 0;
  UINT32             Dtb   = 0;
  CHAR8             *Extra;

  if (Size < 8 || !Same (Header, ANDROID_BOOT_MAGIC, 8)) {
    return FALSE;
  }
  if (!GetLe32 (Header, Size, 8, &KernelSize) ||
      !GetLe32 (Header, Size, 40, &Image.HeaderVersion)) {
    return FALSE;
  }
  if (Image.HeaderVersion >= 3) {
    // v3 and v4 use 4 KiB pages and keep the DTB in vendor_boot.
    if (!GetLe32 (Header, Size, 12, &RamdiskSize) ||
        !GetLe32 (Header, Size, 16, &Os) || !InBounds (Size, 44, 1536)) {
      return FALSE;
    }
    Page          = 4096;
    Image.Cmdline = Cstr (Header + 44, 1536);
  } else {
    if (!GetLe32 (Header, Size, 16, &RamdiskSize) ||
        !GetLe32 (Header, Size, 24, &SecondSize) ||
        !GetLe32 (Header, Size, 36, &Page)) {
      return FALSE;
    }
    if (Page < 2048 || Page > 65536 || (Page & (Page - 1))) {
      return FALSE;
    }
    if (!InBounds (Size, 64, 512) || !InBounds (Size, 608, 1024)) {
      return FALSE;
    }
    if (Image.HeaderVersion == 1 && !GetLe32 (Header, Size, 1632, &Rdtbo)) {
      return FALSE;
    }
    if (Image.HeaderVersion == 2 && (!GetLe32 (Header, Size, 1632, &Rdtbo) ||
                                     !GetLe32 (Header, Size, 1648, &Dtb))) {
      return FALSE;
    }
    if (!GetLe32 (Header, Size, 44, &Os)) {
      return FALSE;
    }
    Image.Cmdline = Cstr (Header + 64, 512);
    Extra         = Cstr (Header + 608, 1024);
    if (Image.Cmdline && Extra && *Extra) {
      CHAR8 *Both = SPrintAlloc ("%s%s", Image.Cmdline, Extra);
      FreePool (Image.Cmdline);
      Image.Cmdline = Both;
    }
    if (!Extra) {
      FreePool (Image.Cmdline);
      Image.Cmdline = NULL;
    }
    FreePool (Extra);
  }
  if (!Image.Cmdline) {
    return FALSE;
  }
  Image.PageSize = Page;
  Image.Kernel   = (ANDROID_SECTION){Page, KernelSize};
  Image.Ramdisk  = (ANDROID_SECTION){
    Image.Kernel.Offset + Align (KernelSize, Page), RamdiskSize};
  if (Image.HeaderVersion < 3) {
    Image.Second = (ANDROID_SECTION){
      Image.Ramdisk.Offset + Align (RamdiskSize, Page), SecondSize};
    Image.RecoveryDtbo =
      (ANDROID_SECTION){Image.Second.Offset + Align (SecondSize, Page), Rdtbo};
    Image.Dtb =
      (ANDROID_SECTION){Image.RecoveryDtbo.Offset + Align (Rdtbo, Page), Dtb};
  }
  Image.HasOsVersion = AndroidDecodeOsVersion (Os, &Image.OsVersion);
  *Out               = Image;
  return TRUE;
}

VOID
AndroidBootImageFree (IN OUT ANDROID_BOOT_IMAGE *Image)
{
  FreePool (Image->Cmdline);
  Image->Cmdline = NULL;
}

UINT64
AndroidBootImageTotalSize (IN CONST ANDROID_BOOT_IMAGE *Image)
{
  CONST ANDROID_SECTION *Sections[] = {&Image->Kernel, &Image->Ramdisk,
                                       &Image->Second, &Image->RecoveryDtbo,
                                       &Image->Dtb};
  UINT64                 Max        = 0;

  for (UINTN Index = 0; Index < sizeof (Sections) / sizeof (*Sections);
       Index++) {
    if (Sections[Index]->Offset + Sections[Index]->Size > Max) {
      Max = Sections[Index]->Offset + Sections[Index]->Size;
    }
  }
  return Max;
}

BOOLEAN
AndroidDecodeOsVersion (
  IN UINT32               Value,
  OUT ANDROID_OS_VERSION *Out
)
{
  if (Value == 0) {
    return FALSE;
  }
  *Out = (ANDROID_OS_VERSION){
    .Major = (UINT8)(Value >> 25 & 0x7f),
    .Minor = (UINT8)(Value >> 18 & 0x7f),
    .Patch = (UINT8)(Value >> 11 & 0x7f),
    .Year  = (UINT16)(2000 + (Value >> 4 & 0x7f)),
    .Month = (UINT8)(Value & 0xf),
  };
  return TRUE;
}

ANDROID_BOOT_COMMAND
AndroidParseBcb (
  IN CONST UINT8 *Misc,
  IN UINTN        Size
)
{
  ANDROID_BOOT_COMMAND Command = {ANDROID_BOOT_COMMAND_NONE, ""};
  OABL_TEXT            Text    = {(CONST CHAR8 *)Misc, 0};

  if (Size > ANDROID_BCB_COMMAND_LEN) {
    Size = ANDROID_BCB_COMMAND_LEN;
  }
  while (Text.Len < Size && Misc[Text.Len]) {
    Text.Len++;
  }
  if (TextEqStr (Text, "boot-recovery")) {
    Command.Kind = ANDROID_BOOT_COMMAND_RECOVERY;
  } else if (TextEqStr (Text, "bootonce-bootloader")) {
    Command.Kind = ANDROID_BOOT_COMMAND_BOOTLOADER_ONCE;
  } else if (Text.Len) {
    // Anything but printable ASCII means an erased or never-written
    // partition, not a command.
    for (UINTN Index = 0; Index < Text.Len; Index++) {
      if (Misc[Index] < 0x21 || Misc[Index] > 0x7e) {
        return Command;
      }
    }
    Command.Kind = ANDROID_BOOT_COMMAND_OTHER;
    for (UINTN Index = 0; Index < Text.Len; Index++) {
      Command.Other[Index] = Text.Ptr[Index];
    }
  }
  return Command;
}

VOID
AndroidBcbCommand (
  IN CONST CHAR8 *Command,
  OUT UINT8       Out[ANDROID_BCB_COMMAND_LEN]
)
{
  UINTN Length = AsciiStrLen (Command);

  if (Length > ANDROID_BCB_COMMAND_LEN - 1) {
    Length = ANDROID_BCB_COMMAND_LEN - 1;
  }
  for (UINTN Index = 0; Index < ANDROID_BCB_COMMAND_LEN; Index++) {
    Out[Index] = Index < Length ? (UINT8)Command[Index] : 0;
  }
}

BOOLEAN
AndroidFindDtbs (
  IN CONST UINT8   *Region,
  IN UINTN          Size,
  OUT ANDROID_DTBS *Found
)
{
  UINTN From = 0;

  *Found = (ANDROID_DTBS){0};
  while (From + 8 <= Size) {
    UINTN At    = From;
    UINTN Total = 0;

    while (At + 4 <= Size && !Same (Region + At, mFdtMagic, 4)) {
      At++;
    }
    if (At + 4 > Size) {
      break;
    }
    if (At + 8 <= Size) {
      Total = ReadBe32 (Region + At + 4);
    }
    if (Total >= 64 && Total <= 16 << 20 && Total <= Size - At &&
        DtInspect (Region + At, Total, NULL)) {
      if (!VecPush (Found, (ANDROID_DTB){At, Total})) {
        return FALSE;
      }
      From = At + Total;
    } else {
      From = At + 4;
    }
  }
  return TRUE;
}

BOOLEAN
AndroidDtbWindow (
  IN CONST ANDROID_BOOT_IMAGE *Image,
  OUT UINT64                  *Offset,
  OUT UINT64                  *Length
)
{
  UINT64 KernelSize = Image->Kernel.Size;
  UINT64 Window;

  if (Image->Dtb.Size > 0) {
    *Offset = Image->Dtb.Offset;
    *Length = Image->Dtb.Size;
    return TRUE;
  }
  if (KernelSize == 0) {
    return FALSE;
  }
  Window =
    KernelSize < ANDROID_DTB_TAIL_WINDOW ? KernelSize : ANDROID_DTB_TAIL_WINDOW;
  *Offset = Image->Kernel.Offset + KernelSize - Window;
  *Length = Window;
  return TRUE;
}

BOOLEAN
AndroidWindowTooShort (
  IN CONST ANDROID_BOOT_IMAGE *Image,
  IN UINT64                    Offset,
  IN UINT64                    Length,
  IN CONST UINT8              *Region,
  IN UINTN                     Size
)
{
  (VOID)Offset;
  return Image->Dtb.Size == 0 && Length < Image->Kernel.Size && Size >= 4 &&
         Same (Region, mFdtMagic, 4);
}
