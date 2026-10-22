/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Models - scanning the boot drives for device models.

  A drive's models come from the .dtb files in \boot\grub (GRUB-based
  images), \dtbs, \dtbs\qcom, \dtb and \dtb\qcom (dtbloader,
  distributions, U-Boot), or else from the devicetrees appended to its
  first boot image file. When XBL's file system
  driver fails, boot images are read straight off FAT32.
*/

#ifndef __EFI_MODELS_H__
#define __EFI_MODELS_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Efi/Serial.h"

// The boot image files, in the order the ABL tries them.
extern CONST CHAR8 *CONST gModelsBootImages[4];

// Returns TRUE if Path on Fs starts with the Android boot image magic.
BOOLEAN
ModelsIsBootImage (
  IN EFI_HANDLE   Fs,
  IN CONST CHAR8 *Path
);

// A model name and its panel rotation in degrees, if the devicetree has one.
typedef struct _SCANNED_MODEL {
  CHAR8  *Name;
  BOOLEAN HasRotation;
  UINT16  Rotation;
} SCANNED_MODEL;

typedef struct _SCANNED_MODELS OABL_VEC (SCANNED_MODEL) SCANNED_MODELS;

VOID
ModelsFree (IN OUT SCANNED_MODELS *Models);

// Reads the devicetree for Model on Fs: the first matching .dtb file in
// those folders, else the matching blob in the boot image.
// FALSE if there is none. Caller frees Out with BufferFree.
BOOLEAN
ModelsDevicetreeFor (
  IN EFI_HANDLE    Fs,
  IN CONST CHAR8  *Model,
  OUT OABL_BUFFER *Out
);

// Adds the models found on Drives to Out, which starts empty, and sorts them
// by name. When drives share a model, the first drive's devicetree wins.
// Logs one line per folder or boot image that had any. Free Out with
// ModelsFree.
VOID
ModelsScan (
  IN CONST EFI_HANDLE   *Drives,
  IN UINTN               Count,
  IN LOG_SINK            Log,
  IN OUT SCANNED_MODELS *Out
);

#endif
