/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-source-exception */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

#ifndef __OPENABL_MENU_H__
#define __OPENABL_MENU_H__

#include <Uefi.h>
#include <Library/BootLinux.h>
#include <Protocol/OpenAblMenuHost.h>

/* Lets the OpenABL program run the boot */
EFI_STATUS
OpenAblMenuMain (IN CONST BootInfo *Info, IN OUT BOOLEAN *Fastboot,
                 IN OUT BOOLEAN *Recovery, IN UINT32 Key,
                 IN BOOLEAN SetRotAndBootStateAndVBH);

VOID
OpenAblMenuEarly (VOID);

VOID
OpenAblBootPrep (IN UINT32 Flags);

BOOLEAN
OpenAblAndroidParam (IN UINTN Index, OUT CONST CHAR8 **Key,
                     OUT CONST CHAR8 **Value);

#endif /* __OPENABL_MENU_H__ */
