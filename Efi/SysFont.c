/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SysFont - registers XBL's scaled system fonts when XBL has none.
*/

#include "Efi/SysFont.h"

#ifdef OABL_SYSFONT

#include <Protocol/HiiDatabase.h>
#include <Protocol/HiiFont.h>

#include "Base/Buffer.h"
#include "Base/Print.h"
#include "Core/SysFontGlyphs.h"
#include "Efi/Efi.h"
#include "Efi/Quirks.h"

#define ANY_SIZE_OR_STYLE (EFI_FONT_INFO_ANY_SIZE | EFI_FONT_INFO_ANY_STYLE)
_Static_assert (
  ANY_SIZE_OR_STYLE == (0x00020000 | 0x00040000),
  "ANY_SIZE_OR_STYLE: the UEFI spec's values"
);

STATIC BOOLEAN mDone;

// Writes "fonts: " and the text to the ABL's logger.
STATIC VOID __attribute__ ((format (
  printf,
  2,
  3
)))
Line (
  IN OPENABL_STORAGE_LOG Log OPTIONAL,
  IN CONST CHAR8        *Format,
  ...
)
{
  CHAR8   Buffer[96] = "fonts: ";
  UINTN   Prefix     = AsciiStrLen (Buffer);
  va_list Args;

  va_start (Args, Format);
  VSPrint (Buffer + Prefix, sizeof (Buffer) - Prefix, Format, Args);
  va_end (Args);
  QuirksToAbl (Log, Buffer);
}

// Whether the HII database has a font named Name, in any size or style.
STATIC BOOLEAN
Registered (
  IN EFI_HII_FONT_PROTOCOL *Font,
  IN CONST CHAR8           *Name
)
{
  // EFI_FONT_DISPLAY_INFO: foreground and background (4 bytes each),
  // FontInfoMask, then EFI_FONT_INFO with FontName at byte 18.
  UINT32                 Want[16] = {0};
  UINTN                  Length   = AsciiStrLen (Name);
  EFI_FONT_HANDLE        Handle   = NULL;
  EFI_FONT_DISPLAY_INFO *Got      = NULL;
  EFI_STATUS             Status;

  if (20 + (Length + 1) * 2 > sizeof (Want)) {
    return FALSE;
  }
  Want[2] = ANY_SIZE_OR_STYLE;
  for (UINTN Index = 0; Index < Length; Index++) {
    WriteLe16 ((UINT8 *)Want + 18 + 2 * Index, (UINT8)Name[Index]);
  }
  Status = Font->GetFontInfo (
    Font, &Handle, (CONST EFI_FONT_DISPLAY_INFO *)Want, &Got, NULL
  );
  // GetFontInfo allocates Got.
  if (Got) {
    gBS->FreePool (Got);
  }
  return Status == EFI_SUCCESS;
}

VOID
SysFontEnsure (IN OPENABL_STORAGE_LOG Log OPTIONAL)
{
  EFI_HII_FONT_PROTOCOL     *Font;
  EFI_HII_DATABASE_PROTOCOL *Database;

  if (mDone) {
    return;
  }
  mDone    = TRUE;
  Font     = LocateProtocol (&gEfiHiiFontProtocolGuid);
  Database = LocateProtocol (&gEfiHiiDatabaseProtocolGuid);
  if (!Font || !Database) {
    return;
  }
  // XBLs with FontDxe have them.
  if (Registered (Font, "sysfont2x")) {
    Line (Log, "scaled fonts already present");
    return;
  }
  for (UINT8 Scale = SYSFONT_SCALE_MIN; Scale <= SYSFONT_SCALE_MAX; Scale++) {
    OABL_BUFFER    List   = {0};
    EFI_HII_HANDLE Handle = NULL;
    EFI_STATUS     Status;

    if (!SysFontPackageList (Scale, &List)) {
      Line (Log, "register sysfont%ux: out of memory", (UINT32)Scale);
      continue;
    }
    // The HII database copies the package list.
    Status = Database->NewPackageList (
      Database, (CONST EFI_HII_PACKAGE_LIST_HEADER *)List.Data, NULL, &Handle
    );
    Line (
      Log, "register sysfont%ux: %s", (UINT32)Scale,
      StatusText (Status, (CHAR8[STATUS_TEXT_SIZE]){0})
    );
    BufferFree (&List);
  }
}

#endif
