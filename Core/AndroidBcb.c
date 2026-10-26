/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  AndroidBcb - the bootloader message in Android's misc partition.
*/

#include "Core/AndroidBcb.h"

#include "Base/Memory.h"
#include "Base/Text.h"

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

VOID
AndroidBcbWipe (OUT UINT8 Out[ANDROID_BCB_SIZE])
{
  STATIC CONST CHAR8 Arguments[] = "recovery\n--wipe_data\n";

  ZeroMem (Out, ANDROID_BCB_SIZE);
  AndroidBcbCommand ("boot-recovery", Out);
  CopyMem (Out + ANDROID_BCB_RECOVERY_AT, Arguments, sizeof (Arguments) - 1);
}
