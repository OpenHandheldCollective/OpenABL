/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootEntries - finds what each filesystem offers to boot.
*/

#include "Boot/BootEntries.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Core/Bls.h"
#include "Core/EfiData.h"
#include "Core/Extlinux.h"
#include "Core/Pe.h"
#include "Base/Buffer.h"
#include "Efi/Files.h"
#include "Efi/Serial.h"

// Size limit for entry files and loader.conf.
#define TEXT_LIMIT ((UINTN)64 << 10)
// Bytes read from the start of a UKI to get its section table.
#define PE_HEAD 4096

CONST CHAR8 *
BootEntriesVolumeKind (IN CONST BOOT_ENTRIES_VOLUME *V)
{
  return V->Removable ? "removable drive" : "internal drive";
}

CHAR8 *
BootEntriesVolumeDescribe (IN CONST BOOT_ENTRIES_VOLUME *V)
{
  return SPrintAlloc ("%s (%s)", V->Name, BootEntriesVolumeKind (V));
}

VOID
BootEntriesVolumeFree (IN OUT BOOT_ENTRIES_VOLUME *V)
{
  BufferFree (&V->Dev);
  FreePool (V->Name);
  FreePool (V->PartUuid);
  BootEntryListFree (&V->Entries);
  *V = (BOOT_ENTRIES_VOLUME){0};
}

VOID
BootEntriesVolumesFree (IN OUT BOOT_ENTRIES_VOLUMES *Volumes)
{
  for (UINTN Index = 0; Index < Volumes->Count; Index++) {
    BootEntriesVolumeFree (&Volumes->Items[Index]);
  }
  VecFree (Volumes);
}

STATIC BOOLEAN
Removable (IN EFI_HANDLE Fs)
{
  // Never open BlockIo exclusively. That would disconnect the filesystem
  // above it.
  EFI_BLOCK_IO_PROTOCOL *BlockIo = GetProtocol (Fs, &gEfiBlockIoProtocolGuid);

  return BlockIo && BlockIo->Media && BlockIo->Media->RemovableMedia;
}

// Returns the GPT partition GUID from the path's HD() node, in systemd's
// uppercase format, or NULL.
STATIC CHAR8 *
PartUuid (IN CONST OABL_BUFFER *Dev)
{
  UEFI_NODE    Node;
  UEFI_NODE    Hd;
  OABL_BYTES   Data;
  CONST UINT8 *Guid;
  BOOLEAN      Found = FALSE;
  UINTN        At    = 0;

  while (UefiNextNode (Dev->Data, Dev->Len, &At, &Node)) {
    if (Node.Type == UEFI_TYPE_MEDIA && Node.SubType == UEFI_MEDIA_HARD_DRIVE) {
      Hd    = Node;
      Found = TRUE;
    }
  }
  if (!Found) {
    return NULL;
  }
  Data = UefiNodeData (&Hd);
  // PartitionNumber(4) Start(8) Size(8) Signature(16) MBRType(1)
  // SignatureType(1)
  if (Data.Len < 38 || Data.Ptr[37] != 0x02) {
    return NULL;
  }
  Guid = Data.Ptr + 20;
  return SPrintAlloc (
    "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", (UINT32)ReadLe32 (Guid),
    (UINT32)ReadLe16 (Guid + 4), (UINT32)ReadLe16 (Guid + 6), Guid[8], Guid[9],
    Guid[10], Guid[11], Guid[12], Guid[13], Guid[14], Guid[15]
  );
}

// Moves Entry into Out. Frees it if out of memory.
STATIC VOID
PushEntry (
  IN OUT BOOT_ENTRY_LIST *Out,
  IN OUT BOOT_ENTRY      *Entry
)
{
  if (!VecPush (Out, *Entry)) {
    BootEntryFree (Entry);
  }
}

STATIC VOID
Type1Entries (
  IN EFI_HANDLE           Fs,
  IN OUT BOOT_ENTRY_LIST *Out
)
{
  FILES_DIR_ENTRIES List = {0};

  FilesList (Fs, BLS_ENTRIES_DIR, &List);
  for (UINTN Index = 0; Index < List.Count; Index++) {
    CONST CHAR8 *Name = List.Items[Index].Name;
    BOOT_ENTRY   Entry;
    CHAR8       *Path;
    CHAR8       *Text;

    if (List.Items[Index].IsDir ||
        !TextEndsWithNoCase (TextOf (Name), ".conf")) {
      continue;
    }
    Path = SPrintAlloc ("%s\\%s", BLS_ENTRIES_DIR, Name);
    if (!Path) {
      continue;
    }
    Text = FilesReadText (Fs, Path, TEXT_LIMIT);
    if (Text) {
      if (BlsType1Entry (Name, TextOf (Text), BLS_ARCH, &Entry)) {
        PushEntry (Out, &Entry);
      } else {
        SerialLog (
          "  %s: skipped (other architecture, or nothing to start)", Path
        );
      }
    }
    FreePool (Text);
    FreePool (Path);
  }
  FilesEntriesFree (&List);
}

// Reads a UKI's section Name as text, up to TEXT_LIMIT bytes. Returns NULL
// if there is no such section.
STATIC CHAR8 *
Section (
  IN EFI_HANDLE         Fs,
  IN CONST CHAR8       *Path,
  IN CONST PE_SECTIONS *Sections,
  IN CONST CHAR8       *Name
)
{
  CONST PE_SECTION *Found = PeFind (Sections->Items, Sections->Count, Name);
  OABL_BUFFER       Data;
  UINT64            Offset;
  UINTN             Len;
  CHAR8            *Text;

  if (!Found) {
    return NULL;
  }
  PeSectionDataRange (Found, &Offset, &Len);
  if (!FilesReadPart (
        Fs, Path, Offset, Len < TEXT_LIMIT ? Len : TEXT_LIMIT, &Data
      )) {
    return NULL;
  }
  Text = Utf8Lossy (Data.Data, Data.Len);
  BufferFree (&Data);
  return Text;
}

STATIC VOID
UkiEntries (
  IN EFI_HANDLE           Fs,
  IN OUT BOOT_ENTRY_LIST *Out
)
{
  FILES_DIR_ENTRIES List = {0};

  FilesList (Fs, BLS_UKI_DIR, &List);
  for (UINTN Index = 0; Index < List.Count; Index++) {
    CONST CHAR8 *Name     = List.Items[Index].Name;
    PE_SECTIONS  Sections = {0};
    OABL_BUFFER  Head;
    BOOT_ENTRY   Entry;
    CHAR8       *Path;
    CHAR8       *Osrel;
    CHAR8       *Uname;

    if (List.Items[Index].IsDir ||
        !TextEndsWithNoCase (TextOf (Name), ".efi")) {
      continue;
    }
    Path = SPrintAlloc ("%s\\%s", BLS_UKI_DIR, Name);
    if (!Path) {
      continue;
    }
    if (!FilesReadPart (Fs, Path, 0, PE_HEAD, &Head)) {
      FreePool (Path);
      continue;
    }
    if (!PeSections (Head.Data, Head.Len, &Sections)) {
      SerialLog ("  %s: not a PE image; skipped", Path);
    } else if (!PeFind (Sections.Items, Sections.Count, ".linux")) {
      SerialLog ("  %s: no .linux section, not a UKI; skipped", Path);
    } else {
      Osrel = Section (Fs, Path, &Sections, ".osrel");
      Uname = Section (Fs, Path, &Sections, ".uname");
      if (BlsUkiEntry (
            Name, TextOf (Osrel ? Osrel : ""),
            Uname ? TextOf (Uname) : (OABL_TEXT){NULL, 0}, &Entry
          )) {
        PushEntry (Out, &Entry);
      }
      FreePool (Osrel);
      FreePool (Uname);
    }
    VecFree (&Sections);
    BufferFree (&Head);
    FreePool (Path);
  }
  FilesEntriesFree (&List);
}

// Reads an extlinux INCLUDE file from the filesystem in Context.
STATIC CHAR8 *
ReadInclude (
  IN OABL_TEXT IncludePath,
  OUT UINTN   *Length,
  IN VOID     *Context
)
{
  EFI_HANDLE Fs   = *(EFI_HANDLE *)Context;
  CHAR8     *Path = BootEntryUefiPath (IncludePath);
  CHAR8     *Text;

  if (!Path) {
    return NULL;
  }
  Text = FilesReadText (Fs, Path, TEXT_LIMIT);
  FreePool (Path);
  if (Text) {
    *Length = AsciiStrLen (Text);
  }
  return Text;
}

// Adds the labels of the first extlinux.conf found in gExtlinuxPaths.
STATIC VOID
ExtlinuxEntries (
  IN EFI_HANDLE           Fs,
  IN OUT BOOT_ENTRY_LIST *Out
)
{
  for (UINTN Index = 0;
       Index < sizeof (gExtlinuxPaths) / sizeof (*gExtlinuxPaths); Index++) {
    CONST CHAR8    *Conf = gExtlinuxPaths[Index];
    EXTLINUX_CONFIG Config;
    BOOT_ENTRY_LIST Entries = {0};
    CHAR8          *Text    = FilesReadText (Fs, Conf, TEXT_LIMIT);

    if (!Text) {
      continue;
    }
    if (ExtlinuxParse (TextOf (Text), ReadInclude, &Fs, &Config)) {
      ExtlinuxConfigEntries (&Config, Conf, &Entries);
      ExtlinuxConfigFree (&Config);
    }
    FreePool (Text);
    SerialLog ("  %s: %zu label(s)", Conf, Entries.Count);
    for (UINTN Index2 = 0; Index2 < Entries.Count; Index2++) {
      PushEntry (Out, &Entries.Items[Index2]);
    }
    VecFree (&Entries);
    return;
  }
}

// Lists everything bootable on one filesystem, in boot order.
STATIC VOID
Scan (
  IN EFI_HANDLE           Fs,
  IN OUT BOOT_ENTRY_LIST *Out
)
{
  CHAR8 *Conf;
  CHAR8 *Pattern = NULL;
  UINTN  Default;

  Type1Entries (Fs, Out);
  UkiEntries (Fs, Out);
  BlsSort (Out->Items, Out->Count);
  Conf = FilesReadText (Fs, BLS_LOADER_CONF, TEXT_LIMIT);
  if (Conf) {
    Pattern = BlsLoaderConfDefault (TextOf (Conf));
  }
  if (BlsPickDefault (Out->Items, Out->Count, Pattern, &Default)) {
    // Move the default to the front, keeping the others in order.
    BOOT_ENTRY Entry = Out->Items[Default];

    for (UINTN Index = Default; Index > 0; Index--) {
      Out->Items[Index] = Out->Items[Index - 1];
    }
    Out->Items[0] = Entry;
  }
  FreePool (Pattern);
  FreePool (Conf);
  ExtlinuxEntries (Fs, Out);
}

STATIC BOOLEAN
RemovableFirst (
  IN CONST VOID *A,
  IN CONST VOID *B,
  IN VOID       *Context
)
{
  (VOID)Context;
  return ((CONST BOOT_ENTRIES_VOLUME *)A)->Removable &&
         !((CONST BOOT_ENTRIES_VOLUME *)B)->Removable;
}

VOID
BootEntriesDiscover (
  IN OABL_BYTES             OwnImage,
  IN BOOLEAN                HasOnly,
  IN CONST EFI_HANDLE      *Only OPTIONAL,
  IN UINTN                  OnlyCount,
  IN BOOLEAN                DefaultFiles,
  OUT BOOT_ENTRIES_VOLUMES *Out
)
{
  EFI_HANDLE       *Firmware = NULL;
  CONST EFI_HANDLE *Handles  = Only;
  UINTN             Count    = OnlyCount;
  OABL_BUFFER       FileNode = {0};

  *Out = (BOOT_ENTRIES_VOLUMES){0};
  if (!HasOnly) {
    Count   = LocateHandles (&gEfiSimpleFileSystemProtocolGuid, &Firmware);
    Handles = Firmware;
  }
  UefiFilePathNode (UEFI_DEFAULT_BOOT_FILE, &FileNode);
  for (UINTN Index = 0; Index < Count; Index++) {
    EFI_HANDLE          Fs          = Handles[Index];
    BOOT_ENTRIES_VOLUME V           = {.Fs = Fs};
    OABL_BUFFER         DefaultPath = {0};
    BOOLEAN             IsSelf;

    if (!PathsHandlePath (Fs, &V.Dev)) {
      continue;
    }
    Scan (Fs, &V.Entries);
    UefiJoin (
      (OABL_BYTES[]){BufferBytes (&V.Dev), BufferBytes (&FileNode)}, 2,
      &DefaultPath
    );
    IsSelf = OwnImage.Len &&
             UefiSameNodes (
               DefaultPath.Data, DefaultPath.Len, OwnImage.Ptr, OwnImage.Len
             );
    BufferFree (&DefaultPath);
    V.DefaultFile =
      DefaultFiles && !IsSelf && FilesExists (Fs, UEFI_DEFAULT_BOOT_FILE);
    V.Name = FilesVolumeLabel (Fs);
    if (!V.Name) {
      V.Name = SPrintAlloc ("drive %zu", Index);
    }
    V.PartUuid  = PartUuid (&V.Dev);
    V.Removable = Removable (Fs);
    SerialLog (
      "volume %s (%s): %zu entr%s%s", V.Name, BootEntriesVolumeKind (&V),
      V.Entries.Count, V.Entries.Count == 1 ? "y" : "ies",
      V.DefaultFile ? ", default file" : ""
    );
    for (UINTN Index2 = 0; Index2 < V.Entries.Count; Index2++) {
      CONST BOOT_ENTRY *Entry = &V.Entries.Items[Index2];
      CHAR8             Counter[64];

      BlsCounterDebug (
        Entry->HasCounter, Entry->Counter, Counter, sizeof (Counter)
      );
      SerialLog (
        "  %s %s \"%s\" %s%s", BootEntrySourceDebug (Entry->Source), Entry->Id,
        Entry->Title, Counter, BootEntryIsBad (Entry) ? " (bad)" : ""
      );
    }
    if (!VecPush (Out, V)) {
      BootEntriesVolumeFree (&V);
    }
  }
  BufferFree (&FileNode);
  if (Firmware) {
    gBS->FreePool (Firmware);
  }
  // SortBy is stable, so firmware order is kept within each group.
  if (!HasOnly) {
    SortBy (Out->Items, Out->Count, sizeof (*Out->Items), RemovableFirst, NULL);
  }
}
