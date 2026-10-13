/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Extlinux - parser for extlinux.conf, the syslinux-style menu U-Boot reads.

  U-Boot's distro boot and Standard Boot read it from /extlinux/ or
  /boot/extlinux/. Many ARM distributions install it. Keywords are
  case-insensitive. Paths are relative to the partition root, with or without a
  leading /, as in U-Boot.
*/

#ifndef __CORE_EXTLINUX_H__
#define __CORE_EXTLINUX_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Base/Text.h"
#include "Core/BootEntry.h"

// Where U-Boot looks for the file, in order.
extern CONST CHAR8 *CONST gExtlinuxPaths[2];

// Keys the label doesn't set are NULL.
typedef struct _EXTLINUX_LABEL {
  CHAR8             *Name;
  CHAR8             *MenuLabel;
  CHAR8             *Kernel;
  BOOT_ENTRY_STRINGS Initrds;
  CHAR8             *Append;
  CHAR8             *Fdt;
  CHAR8             *FdtDir;
  BOOT_ENTRY_STRINGS Overlays;
} EXTLINUX_LABEL;

typedef struct _EXTLINUX_CONFIG {
  CHAR8 *MenuTitle;
  // DEFAULT, a label name.
  CHAR8 *DefaultLabel;
  // The label that contains MENU DEFAULT.
  CHAR8 *MenuDefault;
  // TIMEOUT in tenths of a second, valid if HasTimeout.
  BOOLEAN HasTimeout;
  UINT32  TimeoutDs;
  struct OABL_VEC (EXTLINUX_LABEL) Labels;
} EXTLINUX_CONFIG;

// Include reads an INCLUDE'd file by its path as written and returns its
// text from AllocatePool, or NULL. With a NULL Include, INCLUDE is ignored.
// Includes nest at most four deep.
BOOLEAN
ExtlinuxParse (
  IN OABL_TEXT Text,
  IN CHAR8 *(*Include OPTIONAL) (
    IN OABL_TEXT Path,
    OUT UINTN   *Len,
    IN VOID     *Context
  ),
  IN VOID             *Context,
  OUT EXTLINUX_CONFIG *Out
);

VOID
ExtlinuxConfigFree (IN OUT EXTLINUX_CONFIG *Config);

// Finds the default label: DEFAULT, else MENU DEFAULT, else the first one
// that can boot. Fails if no label can boot.
BOOLEAN
ExtlinuxConfigDefaultIndex (
  IN CONST EXTLINUX_CONFIG *Config,
  OUT UINTN                *Out
);

// Returns the bootable labels as entries, the default first. Conf is the
// path of the file, shown in the menu.
BOOLEAN
ExtlinuxConfigEntries (
  IN CONST EXTLINUX_CONFIG *Config,
  IN CONST CHAR8           *Conf,
  OUT BOOT_ENTRY_LIST      *Out
);

#endif
