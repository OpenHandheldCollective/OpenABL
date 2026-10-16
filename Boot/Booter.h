/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Booter - tries the boot plan in order until an image runs.

  Handles UEFI boot options, Boot Loader Specification entries and UKIs,
  extlinux.conf, Automatic Boot Assessment and the Boot Loader Interface,
  and hands the devicetree over as EBBR asks. The caller supplies policy
  through BOOT_HOST, such as the model's devicetree and the ABL's work just
  before an image starts.
*/

#ifndef __BOOT_BOOTER_H__
#define __BOOT_BOOTER_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Boot/BootManager.h"
#include "Boot/BootEntries.h"

// Platform callbacks, each called with Context. Any of them may be NULL.
typedef struct _BOOT_HOST {
  // Fills Out with the selected model's devicetree, for targets that don't
  // bring their own. The booter frees Out. Returning FALSE leaves the
  // configuration table alone (the ABL installs the model's in BeforeStart).
  BOOLEAN (*ModelDevicetree) (
    IN VOID         *Context,
    OUT OABL_BUFFER *Out
  );
  // Returns the selected model's compatible for extlinux FDTDIR, or NULL.
  // The booter frees it. Defaults to BootHostDefaultModelCompatible.
  CHAR8 *(*ModelCompatible) (IN VOID *Context);
  // Called once an image is loaded and its options are set, just before it
  // starts. Fs is its drive, or NULL. OwnDevicetree is TRUE when the entry
  // brings a devicetree, installed after this call, so the platform need
  // not install the model's. Otherwise what is installed here stays, unless
  // an extlinux FDTDIR finds a better match.
  VOID (*BeforeStart) (
    IN VOID      *Context,
    IN EFI_HANDLE Fs OPTIONAL,
    IN BOOLEAN    OwnDevicetree
  );
  // Called when the started image returns.
  VOID (*AfterReturn) (IN VOID *Context);
  // Called before anything that may not return, so the log can be saved.
  VOID (*Checkpoint) (
    IN VOID        *Context,
    IN CONST CHAR8 *Why
  );
  VOID *Context;
} BOOT_HOST;

// Calls the host's ModelDevicetree. Returns FALSE if it has none.
BOOLEAN
BootHostModelDevicetree (
  IN CONST BOOT_HOST *Host,
  OUT OABL_BUFFER    *Out
);

// Calls the host's ModelCompatible, or BootHostDefaultModelCompatible.
CHAR8 *
BootHostModelCompatible (IN CONST BOOT_HOST *Host);

// Returns the first compatible of the host's ModelDevicetree.
CHAR8 *
BootHostDefaultModelCompatible (IN CONST BOOT_HOST *Host);

typedef struct _BOOT_SCOPE {
  // With Only, search just Volumes, in that order. Otherwise search every
  // drive, removable media first. The booter keeps its own copy of Volumes.
  BOOLEAN           Only;
  CONST EFI_HANDLE *Volumes;
  UINTN             VolumeCount;
  // Also try each drive's \EFI\BOOT\BOOTAA64.EFI (the ABL starts those
  // itself, with its own fallbacks).
  BOOLEAN DefaultFiles;
} BOOT_SCOPE;

typedef enum {
  // An image ran and returned.
  BOOT_FAILURE_OK,
  // There was nothing to try.
  BOOT_FAILURE_NOTHING_FOUND,
  // Everything failed. *Why holds the last reason.
  BOOT_FAILURE_FAILED,
} BOOT_FAILURE;

typedef struct _BOOTER {
  BOOT_MANAGER         Manager;
  BOOT_ENTRIES_VOLUMES Volumes;
  BOOT_MANAGER_TARGETS Plan;
  BOOT_SCOPE           Scope;
  // LoaderInfo (borrowed) and start time for the Boot Loader Interface.
  CONST CHAR8 *Info;
  UINT64       InitUs;
} BOOTER;

// Scans the drives and plans the boot, consuming BootNext and
// LoaderEntryOneShot. Info is reported as LoaderInfo and must outlive Out.
VOID
BooterNew (
  OUT BOOTER          *Out,
  IN CONST CHAR8      *Info,
  IN UINT64            InitUs,
  IN CONST BOOT_SCOPE *Scope
);

VOID
BooterFree (IN OUT BOOTER *Booter);

// Rescans the drives and plans again, as after an image returns.
VOID
BooterRefresh (IN OUT BOOTER *Booter);

// Describes Target for the log and the menu. Caller frees the result.
CHAR8 *
BooterDescribe (
  IN CONST BOOTER              *Booter,
  IN CONST BOOT_MANAGER_TARGET *Target
);

// Tries the plan in order, or only target Only with HasOnly, until an image
// runs. After BOOT_FAILURE_OK, call BooterRefresh before going on, since
// the image may have changed the drives or set BootNext. With
// BOOT_FAILURE_FAILED, *Why gets the last reason. Caller frees it.
BOOT_FAILURE
BooterBoot (
  IN OUT BOOTER      *Booter,
  IN CONST BOOT_HOST *Host,
  IN BOOLEAN          HasOnly,
  IN UINTN            Only,
  OUT CHAR8         **Why OPTIONAL
);

// Installs Dtb as the devicetree configuration table, replacing the
// firmware's. EBBR wants it in EfiACPIReclaimMemory. Returns NULL on
// success, else a static error string.
CONST CHAR8 *
HandoffInstallDevicetree (
  IN CONST UINT8 *Dtb,
  IN UINTN        Size
);

// Copies the installed devicetree configuration table into Out. Returns
// FALSE if there is none. Caller frees Out.
BOOLEAN
HandoffInstalledDevicetree (OUT OABL_BUFFER *Out);

#endif
