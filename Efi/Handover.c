/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Handover - prepares the system just before a booted image starts.
*/

#include "Efi/Handover.h"

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Guid/HobList.h>
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>
#include <Guid/Fdt.h>

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Core/Carveouts.h"
#include "Core/EfiData.h"
#include "Efi/DisplayHandover.h"
#include "Efi/Dxe.h"
#include "Efi/Models.h"
#include "Efi/Qcom.h"
#include "Efi/Splash.h"

#define PAGE 4096ull

// XBL memory regions by uefiplat.cfg label ("Kernel", "Display Reserved",
// ...). They come from the UEFI Config Lib, loaded through XBL's
// shared-library loader, which a GUID HOB points to. The ABL does the same,
// so nothing is hard-coded per SoC.

// XBL's shared-library loader HOB.
STATIC EFI_GUID mShlibHob = {0xbedaeabc,
                             0x5e70,
                             0x4d66,
                             {0x97, 0x33, 0x21, 0x3d, 0x07, 0x2b, 0x9d, 0x04}};

// These layouts must match XBL's.
typedef struct _SHLIB_LOADER {
  UINT32 LoaderVersion;
  VOID  *InstallLib;
  EFI_STATUS (*LoadLib) (
    IN CONST CHAR8 *Name,
    IN UINT32       Flags,
    OUT VOID      **Lib
  );
} SHLIB_LOADER;

typedef struct _MEM_REGION_INFO {
  UINT8  Name[32];
  UINT64 Base;
  UINT64 Size;
  UINT32 BuildHobOption;
  UINT32 ResourceType;
  UINT32 ResourceAttribute;
  UINT32 MemoryType;
  UINT32 CacheAttributes;
} MEM_REGION_INFO;

typedef struct _UEFI_CFG_LIB {
  UINT32 LibVersion;
  EFI_STATUS (*GetMemInfoByName) (
    IN CONST CHAR8      *Name,
    OUT MEM_REGION_INFO *Info
  );
} UEFI_CFG_LIB;

_Static_assert (
  sizeof (MEM_REGION_INFO) == 72,
  "MEM_REGION_INFO: XBL's layout"
);
_Static_assert (
  offsetof (
    SHLIB_LOADER,
    LoadLib
  ) == 16,
  "SHLIB_LOADER: XBL's layout"
);
_Static_assert (
  offsetof (
    UEFI_CFG_LIB,
    GetMemInfoByName
  ) == 8,
  "UEFI_CFG_LIB: XBL's layout"
);

STATIC UEFI_CFG_LIB *mLibFound;
STATIC BOOLEAN       mTried;

// Finds the HOB list XBL passed to DXE in the configuration tables.
STATIC CONST UINT8 *
HobList (VOID)
{
  for (UINTN Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    EFI_CONFIGURATION_TABLE *Table = &gST->ConfigurationTable[Index];
    if (CompareGuid (&Table->VendorGuid, &gEfiHobListGuid)) {
      return Table->VendorTable;
    }
  }
  return NULL;
}

// Returns the data of the first GUID extension HOB for Guid, or NULL.
STATIC CONST UINT8 *
GuidHob (IN CONST EFI_GUID *Guid)
{
  CONST UINT8 *Hob = HobList ();

  if (!Hob) {
    return NULL;
  }
  for (INT32 Index = 0; Index < 65536; Index++) {
    UINT16 Type   = ReadLe16 (Hob);
    UINTN  Length = ReadLe16 (Hob + 2);

    if (Type == EFI_HOB_TYPE_END_OF_HOB_LIST || Length < 8) {
      return NULL;
    }
    if (Type == EFI_HOB_TYPE_GUID_EXTENSION &&
        CompareGuid ((CONST EFI_GUID *)(Hob + 8), Guid)) {
      return Hob + 24;
    }
    Hob += Length;
  }
  return NULL;
}

// Loads the UEFI Config Lib on the first call. Returns NULL if XBL has none.
STATIC UEFI_CFG_LIB *
ConfigLib (VOID)
{
  if (!mTried) {
    CONST UINT8 *Data;

    mTried = TRUE;
    // Same call as the ABL's. The library stays loaded.
    Data = GuidHob (&mShlibHob);
    if (Data) {
      CONST SHLIB_LOADER *Loader  = *(CONST SHLIB_LOADER * CONST *)Data;
      VOID               *Library = NULL;

      if (Loader && Loader->LoadLib &&
          Loader->LoadLib ("UEFI Config Lib", 0, &Library) == EFI_SUCCESS &&
          Library) {
        mLibFound = Library;
      }
    }
  }
  return mLibFound;
}

// Looks up XBL's region Name, truncated to 63 bytes. Base and Size must be
// page-aligned and non-zero. Returns FALSE if XBL has no such region.
STATIC BOOLEAN
XblConfigRegion (
  IN CONST CHAR8 *Name,
  OUT UINT64     *Base,
  OUT UINT64     *Size
)
{
  UEFI_CFG_LIB   *Library   = ConfigLib ();
  MEM_REGION_INFO Info      = {0};
  CHAR8           CName[64] = {0};
  CONST UINT64    PageMask  = 0xFFF;
  UINTN           Length;

  if (!Library || !Library->GetMemInfoByName) {
    return FALSE;
  }
  Length = AsciiStrLen (Name);
  if (Length > sizeof (CName) - 1) {
    Length = sizeof (CName) - 1;
  }
  for (UINTN Index = 0; Index < Length; Index++) {
    CName[Index] = Name[Index];
  }
  if (Library->GetMemInfoByName (CName, &Info) != EFI_SUCCESS || !Info.Base ||
      !Info.Size || Info.Base & PageMask || Info.Size & PageMask) {
    return FALSE;
  }
  *Base = Info.Base;
  *Size = Info.Size;
  return TRUE;
}

// Executable code allocations
//
// GRUB allocates its heap as EfiLoaderCode and runs module code from it.
// Without EFI_MEMORY_ATTRIBUTE_PROTOCOL it assumes that memory is
// executable. The SDM845's XBL (BOOT.XF.2.0) maps DRAM execute-never, never
// clears XN on an allocation, and ignores a SetMemoryAttributes that only
// changes XN, so GRUB faults and XBL hangs. If a fresh EfiLoaderCode page
// is XN, we clear XN on all free and EfiLoaderCode memory in the page
// tables ourselves. XBL never sets it again.

#ifdef OABL_LOADER_QUIRKS

#include "Core/PageTable.h"

// Gets the current exception level's translation tables. Fails unless they
// use the 4 KiB granule.
STATIC BOOLEAN
Tables (OUT PAGE_TABLE *Out)
{
#if defined(__aarch64__)
  UINT64  El;
  UINT64  Ttbr;
  UINT64  Tcr;
  BOOLEAN El2;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r"(El));
  El2 = ((El >> 2) & 3) == 2;
  if (El2) {
    __asm__ volatile ("mrs %0, ttbr0_el2" : "=r"(Ttbr));
    __asm__ volatile ("mrs %0, tcr_el2" : "=r"(Tcr));
  } else {
    __asm__ volatile ("mrs %0, ttbr0_el1" : "=r"(Ttbr));
    __asm__ volatile ("mrs %0, tcr_el1" : "=r"(Tcr));
  }
  return PageTableFrom (Ttbr, Tcr, El2, Out);
#else
  (VOID)Out;
  return FALSE;
#endif
}

STATIC VOID
Invalidate (IN BOOLEAN El2)
{
#if defined(__aarch64__)
  if (El2) {
    __asm__ volatile ("dsb ishst\n\ttlbi alle2is\n\tdsb ish\n\tisb" ::
                        : "memory");
  } else {
    __asm__ volatile ("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::
                        : "memory");
  }
#else
  (VOID)El2;
#endif
}

// Checks whether a fresh EfiLoaderCode page is XN. Returns FALSE if its
// mapping isn't found.
STATIC BOOLEAN
CodeAllocXn (
  IN CONST PAGE_TABLE *Table,
  OUT BOOLEAN         *Xn
)
{
  EFI_PHYSICAL_ADDRESS Page = 0;
  BOOLEAN              Found;

  if (gBS->AllocatePages (AllocateAnyPages, EfiLoaderCode, 1, &Page) !=
      EFI_SUCCESS) {
    return FALSE;
  }
  Found = PageTableIsXn (Table, Page, Xn);
  gBS->FreePages (Page, 1);
  return Found;
}

// Clears XN over all free and EfiLoaderCode memory in the memory map.
// Changed gets the number of page table entries changed, Bytes the memory
// covered.
STATIC VOID
ClearFreeAndLoaderCode (
  IN CONST PAGE_TABLE *Table,
  OUT UINT64          *Changed,
  OUT UINT64          *Bytes
)
{
  UINTN  Count;
  UINTN  DescriptorSize;
  UINT8 *Map = MemoryMapGet (&Count, &DescriptorSize);

  *Changed = *Bytes = 0;
  for (UINTN Index = 0; Map && Index < Count; Index++) {
    CONST EFI_MEMORY_DESCRIPTOR *Descriptor =
      MEMORY_MAP_AT (Map, DescriptorSize, Index);
    UINT64 Start = Descriptor->PhysicalStart;
    UINT64 End   = Start + Descriptor->NumberOfPages * PAGE;

    if (Descriptor->Type == EfiConventionalMemory ||
        Descriptor->Type == EfiLoaderCode) {
      *Changed += PageTableClearXn (Table, Start, End);
      *Bytes += End - Start;
    }
  }
  FreePool (Map);
}

// Makes code allocations executable if the firmware leaves them XN.
STATIC VOID
ExecMemoryEnsure (IN LOG_SINK Log)
{
  PAGE_TABLE Table;
  BOOLEAN    Xn;
  BOOLEAN    Found;
  UINT64     Changed;
  UINT64     Bytes;

  if (!Tables (&Table)) {
    LogLine (Log, "exec: not a 4 KiB-granule translation: left as it is");
    return;
  }
  if (!CodeAllocXn (&Table, &Xn)) {
    LogLine (
      Log, "exec: an EfiLoaderCode page's mapping not found: left as it is"
    );
    return;
  }
  if (!Xn) {
    return;
  }
  ClearFreeAndLoaderCode (&Table, &Changed, &Bytes);
  Invalidate (Table.El2);
  Found = CodeAllocXn (&Table, &Xn);
  LogPrint (
    Log,
    "exec: the firmware leaves EfiLoaderCode execute-never (XBL DEP): free "
    "and loader-code memory made executable (%llu MiB, %llu descriptors); a "
    "code page now %s",
    Bytes / (1 << 20), Changed,
    !Found ? "not found"
    : Xn   ? "STILL execute-never"
           : "executable"
  );
}

#endif

#define STATUS_STR(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

// Carveouts
//
// XBL doesn't know the SoC's no-map carveouts and keeps free memory in
// them, so what it gives the OS there is lost once a remote processor
// starts.

// The SoC's no-map carveouts, or NULL if the chip has no table.
STATIC CONST CARVEOUTS_TABLE *
ChipCarveouts (VOID)
{
  QCOM_CHIP Chip;

  if (!QcomChip (&Chip)) {
    return NULL;
  }
  return CarveoutsForChip (
    Chip.Id.Status == EFI_SUCCESS ? Chip.Id.Value : 0, Chip.Name
  );
}

// Marks the free memory in the carveouts as reserved. Linux keeps out of
// them anyway.
STATIC VOID
ReserveCarveouts (IN LOG_SINK Log)
{
  CONST CARVEOUTS_TABLE *Table = ChipCarveouts ();
  UINT64                 Bytes = 0;
  UINTN                  Count;
  UINTN                  DescriptorSize;
  UINT8                 *Map;

  if (!Table || !(Map = MemoryMapGet (&Count, &DescriptorSize))) {
    return;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    CONST EFI_MEMORY_DESCRIPTOR *Descriptor =
      MEMORY_MAP_AT (Map, DescriptorSize, Index);
    UINT64 Start = Descriptor->PhysicalStart;
    UINT64 End   = Start + Descriptor->NumberOfPages * PAGE;

    if (Descriptor->Type != EfiConventionalMemory) {
      continue;
    }
    for (UINTN Region = 0; Region < Table->Count; Region++) {
      UINT64 Base = Table->Regions[Region].Base;
      UINT64 To   = MIN (End, Base + Table->Regions[Region].Len) / PAGE * PAGE;
      EFI_PHYSICAL_ADDRESS At = (MAX (Start, Base) + PAGE - 1) / PAGE * PAGE;

      if (To > At && gBS->AllocatePages (
                       AllocateAddress, EfiReservedMemoryType,
                       (UINTN)((To - At) / PAGE), &At
                     ) == EFI_SUCCESS) {
        Bytes += To - At;
      }
    }
  }
  FreePool (Map);
  if (Bytes) {
    LogPrint (
      Log, "handover: %llu KiB of free memory in the carveouts reserved",
      Bytes / 1024
    );
  }
}

// Allocates Pages pages of Type outside the carveouts, as high as possible.
// AllocateAnyPages would take them from XBL's area for the type, which can
// lie in a carveout where ReserveCarveouts doesn't reach (on SM6115 in the
// ADSP's).
STATIC EFI_STATUS
AllocateOutsideCarveouts (
  IN EFI_MEMORY_TYPE        Type,
  IN UINTN                  Pages,
  OUT EFI_PHYSICAL_ADDRESS *Out
)
{
  CONST CARVEOUTS_TABLE *Table = ChipCarveouts ();
  UINT64                 Bytes = (UINT64)Pages * PAGE;
  UINT64                 Best  = 0;
  UINTN                  Count;
  UINTN                  DescriptorSize;
  UINT8                 *Map;

  if (!Table) {
    return gBS->AllocatePages (AllocateAnyPages, Type, Pages, Out);
  }
  Map = MemoryMapGet (&Count, &DescriptorSize);
  for (UINTN Index = 0; Map && Index < Count; Index++) {
    CONST EFI_MEMORY_DESCRIPTOR *Descriptor =
      MEMORY_MAP_AT (Map, DescriptorSize, Index);
    UINT64 Start = Descriptor->PhysicalStart;
    UINT64 End   = Start + Descriptor->NumberOfPages * PAGE;
    UINT64 At    = End >= Start + Bytes ? End - Bytes : 0;

    if (Descriptor->Type != EfiConventionalMemory || End < Start + Bytes) {
      continue;
    }
    // Move below each carveout it overlaps, until it fits.
    for (BOOLEAN Moved = TRUE; Moved && At >= Start;) {
      Moved = FALSE;
      for (UINTN Region = 0; Region < Table->Count; Region++) {
        UINT64 Base = Table->Regions[Region].Base;
        UINT64 Top  = Base + Table->Regions[Region].Len;

        if (At < Top && At + Bytes > Base) {
          At    = Base >= Bytes ? (Base - Bytes) / PAGE * PAGE : 0;
          Moved = TRUE;
        }
      }
      if (At < Start) {
        break;
      }
    }
    if (At >= Start && At + Bytes <= End && At > Best) {
      Best = At;
    }
  }
  FreePool (Map);
  if (!Best) {
    return EFI_OUT_OF_RESOURCES;
  }
  *Out = Best;
  return gBS->AllocatePages (AllocateAddress, Type, Pages, Out);
}

// Firmware tables for the OS: an ESRT with the ABL's version, and on chips
// whose XBL lacks runtime services an EFI_RT_PROPERTIES_TABLE that says
// which.

#define RT_NO_VMAP (UEFI_RT_SET_VIRTUAL_ADDRESS_MAP | UEFI_RT_CONVERT_POINTER)

// The runtime services XBL lacks, by ChipInfo id: SDM845, SDA845, SM6115,
// QCS6125. SetVirtualAddressMap hangs on SDM845 (a runtime driver fails an
// ASSERT) and SM6115. On SM6115 XBL's runtime code is in the ADSP
// carveout, so none can be called.
STATIC CONST struct {
  UINT32 ChipId;
  UINT32 Unsupported;
} mRuntimeGaps[] = {
  {321, RT_NO_VMAP},
  {341, RT_NO_VMAP},
  {444, UEFI_RT_ALL},
  {467, RT_NO_VMAP},
};

STATIC EFI_GUID mRtPropertiesGuid = {
  0xeb66918a,
  0x7eef,
  0x402a,
  {0x84, 0x2e, 0x93, 0x1d, 0x21, 0xc3, 0x8a, 0xe9}};
STATIC EFI_GUID mEsrtGuid = {0xb122a263,
                             0x3661,
                             0x4f68,
                             {0x99, 0x29, 0x78, 0xf8, 0xb0, 0xd6, 0x21, 0x80}};
// The ABL's firmware class in the ESRT. The ChipInfo id goes in its last
// four bytes, so each chipset's ABL has its own.
STATIC CONST EFI_GUID mAblClass = {
  0xc02d2ab2,
  0x3223,
  0x42be,
  {0x96, 0x08, 0xe2, 0x80, 0x00, 0x00, 0x00, 0x00}};

// ChipInfo's id, or 0.
STATIC UINT32
ChipId (VOID)
{
  QCOM_CHIP Chip;

  return QcomChip (&Chip) && Chip.Id.Status == EFI_SUCCESS ? Chip.Id.Value : 0;
}

// The runtime services XBL lacks on chip Id, or 0 if it isn't listed.
STATIC UINT32
RuntimeGaps (IN UINT32 Id)
{
  for (UINTN Index = 0; Index < sizeof (mRuntimeGaps) / sizeof (*mRuntimeGaps);
       Index++) {
    if (Id == mRuntimeGaps[Index].ChipId) {
      return mRuntimeGaps[Index].Unsupported;
    }
  }
  return 0;
}

// Installs Size bytes (up to a page) from Data as configuration table Guid,
// on an ACPI-reclaim page the OS keeps. *Kept holds the page across boots
// that return.
STATIC EFI_STATUS
InstallTable (
  IN EFI_GUID   *Guid,
  IN CONST VOID *Data,
  IN UINTN       Size,
  IN OUT VOID  **Kept
)
{
  EFI_PHYSICAL_ADDRESS Page = 0;

  if (!*Kept) {
    if (AllocateOutsideCarveouts (EfiACPIReclaimMemory, 1, &Page) !=
        EFI_SUCCESS) {
      return EFI_OUT_OF_RESOURCES;
    }
    *Kept = (VOID *)(UINTN)Page;
  }
  CopyMem (*Kept, Data, Size);
  return gBS->InstallConfigurationTable (Guid, *Kept);
}

STATIC VOID
FirmwareTables (
  IN CONST CHAR8 *Version,
  IN LOG_SINK     Log
)
{
  STATIC VOID *RtKept;
  STATIC VOID *EsrtKept;
  UINT32       Id   = ChipId ();
  UINT32       Gaps = RuntimeGaps (Id);
  UINT8        Rt[8];
  UINT8        Esrt[UEFI_ESRT_ONE_SIZE];
  EFI_GUID     Class = mAblClass;
  EFI_STATUS   Status;

  if (Gaps) {
    UefiRtPropertiesTable (UEFI_RT_ALL & ~Gaps, Rt);
    Status = InstallTable (&mRtPropertiesGuid, Rt, sizeof (Rt), &RtKept);
    LogPrint (
      Log,
      "handover: EFI_RT_PROPERTIES_TABLE 0x%04x (unsupported 0x%04x) "
      "at 0x%llx: %s",
      UEFI_RT_ALL & ~Gaps, Gaps, (UINT64)(UINTN)RtKept, STATUS_STR (Status)
    );
  }
  WriteLe32 (&Class.Data4[4], Id);
  UefiEsrtOne (
    (CONST UINT8 *)&Class, UEFI_ESRT_FW_TYPE_SYSTEM, UefiVersionPair (Version),
    Esrt
  );
  Status = InstallTable (&mEsrtGuid, Esrt, sizeof (Esrt), &EsrtKept);
  LogPrint (
    Log, "handover: ESRT: ABL %s as firmware version 0x%08x at 0x%llx: %s",
    Version ? Version : "?", UefiVersionPair (Version), (UINT64)(UINTN)EsrtKept,
    STATUS_STR (Status)
  );
}

#ifdef OABL_LOADER_QUIRKS

STATIC EFI_STATUS EFIAPI
Refuse (
  IN UINTN                  MemoryMapSize,
  IN UINTN                  DescriptorSize,
  IN UINT32                 DescriptorVersion,
  IN EFI_MEMORY_DESCRIPTOR *VirtualMap
)
{
  return EFI_UNSUPPORTED;
}

// Makes SetVirtualAddressMap refuse on the chips mRuntimeGaps lists, for
// loaders that ignore the runtime properties table. Linux's stub then
// keeps the runtime services at their physical addresses, as with
// efi=novamap. Refuse is in our image, which stays intact until the OS
// reuses the memory.
STATIC VOID
VirtualMapRefuseOnTheseChips (IN LOG_SINK Log)
{
  EFI_RUNTIME_SERVICES *Rt;

  if (!(RuntimeGaps (ChipId ()) & UEFI_RT_SET_VIRTUAL_ADDRESS_MAP)) {
    return;
  }
  // The table is in writable runtime data.
  Rt = gST->RuntimeServices;
  if (!Rt) {
    return;
  }
  Rt->SetVirtualAddressMap = Refuse;
  UefiTableHeaderCrc ((UINT8 *)Rt);
  LogLine (
    Log, "handover: SetVirtualAddressMap refused (this chip's XBL stops inside "
         "it): the OS keeps the runtime services at their physical addresses"
  );
}

#endif

// Handover

// Free space after the devicetree, for the OS loader's /chosen.
#define DTB_PAD 8192

// Hypervisor call that stops it logging to the UART.
#define HYP_DISABLE_UART_LOGGING 0x86000000ull

// XBL's kernel staging region, freed until the image returns.
STATIC UINT64 mStagingBase, mStagingLen;

STATIC VOID
Reclaim (IN LOG_SINK Log);
STATIC VOID
ReleaseKernelStaging (IN LOG_SINK Log);
STATIC VOID
ReleaseDeadRegions (
  IN BOOLEAN  DevMode,
  IN LOG_SINK Log
);

#ifdef OABL_DISPLAY_OFF
STATIC EFI_EVENT mDisplayAtExit;

STATIC VOID EFIAPI
DisplayAtExit (
  IN EFI_EVENT Event,
  IN VOID     *Context
)
{
  (VOID)Event;
  (VOID)Context;
  DisplayHandoverBeforeExit (TRUE, NULL);
}
#endif

VOID
HandoverPrepare (
  IN CONST HANDOVER_PREPARE *Prepare,
  IN LOG_SINK                Log
)
{
  // First, so nothing allocated for the OS below lands in a carveout.
  ReserveCarveouts (Log);
  FirmwareTables (Prepare->Version, Log);
  if (Prepare->Vm) {
    HandoverHypUartOff ();
  }
  if (Prepare->Fs && !Prepare->OwnDevicetree && Prepare->Model &&
      *Prepare->Model) {
    OABL_BUFFER Dtb = {0};

    if (ModelsDevicetreeFor (Prepare->Fs, Prepare->Model, &Dtb)) {
      UINTN        Size;
      CONST VOID  *At;
      CONST CHAR8 *Error =
        HandoverInstallDevicetree (Dtb.Data, Dtb.Len, Log, &Size, &At);

      LogPrint (
        Log, "handover: devicetree for \"%s\" (%llu bytes): %s", Prepare->Model,
        (UINT64)Dtb.Len, Error ? Error : "ok"
      );
    } else {
      LogPrint (
        Log, "handover: no devicetree for \"%s\" on the drive", Prepare->Model
      );
    }
    BufferFree (&Dtb);
  }
  Reclaim (Log);
  ReleaseKernelStaging (Log);
  ReleaseDeadRegions (Prepare->DevMode, Log);
#ifdef OABL_LOADER_QUIRKS
  ExecMemoryEnsure (Log);
  VirtualMapRefuseOnTheseChips (Log);
#endif
#ifdef OABL_DISPLAY_OFF
  if (!mDisplayAtExit) {
    EFI_STATUS Status = gBS->CreateEvent (
      EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_CALLBACK, DisplayAtExit, NULL,
      &mDisplayAtExit
    );

    if (EFI_ERROR (Status)) {
      mDisplayAtExit = NULL;
    }
    LogPrint (
      Log, "handover: display handover at ExitBootServices: %s",
      STATUS_STR (Status)
    );
  }
#endif
}

VOID
HandoverBootReturned (IN LOG_SINK Log)
{
  EFI_PHYSICAL_ADDRESS Base = mStagingBase;
  UINT64               Len  = mStagingLen;
  EFI_STATUS           Status;

#ifdef OABL_DISPLAY_OFF
  if (mDisplayAtExit) {
    gBS->CloseEvent (mDisplayAtExit);
    mDisplayAtExit = NULL;
  }
#endif
  mStagingBase = mStagingLen = 0;
  if (Len == 0) {
    return;
  }
  Status = gBS->AllocatePages (
    AllocateAddress, EfiReservedMemoryType, (UINTN)(Len / PAGE), &Base
  );
  LogPrint (
    Log, "handover: kernel staging region reserved again: %s",
    STATUS_STR (Status)
  );
}

VOID
HandoverHypUartOff (VOID)
{
#if defined(__aarch64__)
  // SMCCC fast call. It may clobber x0-x17 and nothing else.
  register UINT64 X0 __asm__ ("x0") = HYP_DISABLE_UART_LOGGING;
  register UINT64 X1 __asm__ ("x1") = 0;
  register UINT64 X2 __asm__ ("x2") = 0;
  register UINT64 X3 __asm__ ("x3") = 0;
  register UINT64 X4 __asm__ ("x4") = 0;
  register UINT64 X5 __asm__ ("x5") = 0;
  register UINT64 X6 __asm__ ("x6") = 0;
  register UINT64 X7 __asm__ ("x7") = 0;

  __asm__ volatile ("smc #0"
                    : "+r"(X0), "+r"(X1), "+r"(X2), "+r"(X3), "+r"(X4),
                      "+r"(X5), "+r"(X6), "+r"(X7)
                    :
                    : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                      "x16", "x17", "memory");
#endif
}

// The devicetree table installed last. It is freed when replaced, since the
// OS keeps ACPI-reclaim memory.
STATIC EFI_PHYSICAL_ADDRESS mLastDtb;
STATIC UINTN                mLastDtbPages;

CONST CHAR8 *
HandoverInstallDevicetree (
  IN CONST UINT8  *Dtb,
  IN UINTN         DtbSize,
  IN LOG_SINK      Log,
  OUT UINTN       *Size,
  OUT CONST VOID **At
)
{
  OABL_BUFFER          Blob   = {0};
  EFI_PHYSICAL_ADDRESS Memory = 0;
  EFI_PHYSICAL_ADDRESS Prev;
  UINTN                Pages;
  UINTN                PrevPages;

  if (!UefiPaddedDtb (Dtb, DtbSize, DTB_PAD, &Blob)) {
    return "not a devicetree blob";
  }
  SplashReserveSplash (Blob.Data, Blob.Len, Log);
  Pages = (Blob.Len + PAGE - 1) / PAGE;
  if (AllocateOutsideCarveouts (EfiACPIReclaimMemory, Pages, &Memory) !=
      EFI_SUCCESS) {
    BufferFree (&Blob);
    return "out of memory for the devicetree";
  }
  CopyMem ((VOID *)(UINTN)Memory, Blob.Data, Blob.Len);
  if (gBS->InstallConfigurationTable (&gFdtTableGuid, (VOID *)(UINTN)Memory) !=
      EFI_SUCCESS) {
    gBS->FreePages (Memory, Pages);
    BufferFree (&Blob);
    return "InstallConfigurationTable failed";
  }
  Prev          = mLastDtb;
  PrevPages     = mLastDtbPages;
  mLastDtb      = Memory;
  mLastDtbPages = Pages;
  if (Prev) {
    gBS->FreePages (Prev, PrevPages);
  }
  *Size = Blob.Len;
  *At   = (CONST VOID *)(UINTN)Memory;
  BufferFree (&Blob);
  return NULL;
}

BOOLEAN
HandoverDisplayRegion (
  OUT UINT64 *Base,
  OUT UINT64 *Len
)
{
  return XblConfigRegion ("Display Reserved", Base, Len) ||
         XblConfigRegion ("Display_Reserved", Base, Len);
}

// Adds Span as system memory, removing it first if reserved, and allocates
// its whole pages as EfiLoaderData so the OS gets them.
STATIC VOID
Promote (
  IN CARVEOUTS_SPAN Span,
  IN LOG_SINK       Log
)
{
  UINT64     Len = Span.End - Span.Start;
  EFI_STATUS Status;

  if (Span.Reserved) {
    (VOID)DxeRemove (Span.Start, Len);
  }
  Status = DxeAddSystemMemory (Span.Start, Len);
  if (Status == EFI_SUCCESS) {
    EFI_PHYSICAL_ADDRESS First = (Span.Start + PAGE - 1) / PAGE * PAGE;
    UINT64 Pages = (Span.End > First ? Span.End - First : 0) / PAGE;

    if (Pages != 0) {
      Status = gBS->AllocatePages (
        AllocateAddress, EfiLoaderData, (UINTN)Pages, &First
      );
    }
  }
  LogPrint (
    Log, "handover: promote DRAM (%s) @0x%llx 0x%llx: %s",
    Span.Reserved ? "reserved" : "hole", (UINT64)Span.Start, (UINT64)Len,
    STATUS_STR (Status)
  );
}

// Hands DRAM the firmware left out to the OS, using the SoC's carveout table
// or else the gaps between RAM banks.
STATIC VOID
Reclaim (IN LOG_SINK Log)
{
  QCOM_BANKS             Banks = {0};
  DXE_GCD_MAP            Gcd   = {0};
  CARVEOUTS_SPANS        Spans = {0};
  CARVEOUTS_REGION       Display;
  CONST CARVEOUTS_TABLE *Table = NULL;
  QCOM_CHIP              Chip;
  BOOLEAN                HaveBanks = QcomRamBanks (&Banks);
  BOOLEAN                HaveGcd   = DxeMemorySpaceMap (&Gcd);
  BOOLEAN                HaveDisplay;

  if (!HaveBanks || !HaveGcd) {
    LogPrint (Log, "handover: no RAM banks or GCD map: nothing reclaimed");
    VecFree (&Banks);
    VecFree (&Gcd);
    return;
  }
  HaveDisplay = HandoverDisplayRegion (&Display.Base, &Display.Len);
  if (HaveDisplay) {
    LogPrint (
      Log, "handover: display reserved @0x%llx 0x%llx exempt from promote",
      (UINT64)Display.Base, (UINT64)Display.Len
    );
  }
  if (QcomChip (&Chip)) {
    Table = CarveoutsForChip (
      Chip.Id.Status == EFI_SUCCESS ? Chip.Id.Value : 0, Chip.Name
    );
  }
  if (Table) {
    LogPrint (
      Log, "handover: reclaim by the SoC carveout table (%llu no-map regions)",
      (UINT64)Table->Count
    );
    CarveoutsSpansFromTable (
      Banks.Items, Banks.Count, Table->Regions, Table->Count, Gcd.Items,
      Gcd.Count, HaveDisplay ? &Display : NULL, &Spans
    );
  } else {
    LogPrint (Log, "handover: reclaim by bank gap fill");
    CarveoutsSpansFromBanks (
      Banks.Items, Banks.Count, Gcd.Items, Gcd.Count,
      HaveDisplay ? &Display : NULL, &Spans
    );
  }
  for (UINTN Index = 0; Index < Spans.Count; Index++) {
    Promote (Spans.Items[Index], Log);
  }
  VecFree (&Spans);
  VecFree (&Banks);
  VecFree (&Gcd);
}

// Frees XBL's kernel staging region ("Kernel") until the image returns.
STATIC VOID
ReleaseKernelStaging (IN LOG_SINK Log)
{
  UINT64     Base;
  UINT64     Len;
  EFI_STATUS Status;

  mStagingLen = 0;
  if (!XblConfigRegion ("Kernel", &Base, &Len) || Base == 0) {
    return;
  }
  // Nothing uses it once an image runs.
  Status = gBS->FreePages (Base, (UINTN)(Len / PAGE));
  LogPrint (
    Log, "handover: release kernel staging region @0x%llx 0x%llx: %s",
    (UINT64)Base, (UINT64)Len, STATUS_STR (Status)
  );
  if (Status == EFI_SUCCESS) {
    mStagingBase = Base;
    mStagingLen  = Len;
  }
}

// XBL regions unused after boot, and the memory type each is handed over
// as. KeepInDevMode regions are kept for debugging in verbose mode.
STATIC CONST struct {
  CONST CHAR8    *Name;
  EFI_MEMORY_TYPE Type;
  BOOLEAN         KeepInDevMode;
} mDeadRegions[] = {
  // SM8550 and below.
  {"ABOOT FV", EfiBootServicesCode, FALSE},
  {"MPSS EFS", EfiBootServicesData, FALSE},
  {"Log Buffer", EfiBootServicesData, TRUE},
  // Not "Display Reserved": it holds the splash, and freeing it gains little.
  {"XBL Ramdump", EfiBootServicesData, TRUE},
  // SM8650 and up (UEFI_FD and FV_Region aren't freed without device
  // testing).
  {"Log_Buffer", EfiBootServicesData, TRUE},
  {"XBL_Ramdump", EfiBootServicesData, TRUE},
};

STATIC VOID
ReleaseDeadRegions (
  IN BOOLEAN  DevMode,
  IN LOG_SINK Log
)
{
  for (UINTN Index = 0; Index < sizeof (mDeadRegions) / sizeof (*mDeadRegions);
       Index++) {
    UINT64     Base;
    UINT64     Len;
    UINT32     Kind;
    UINTN      Pages;
    EFI_TPL    Tpl;
    EFI_STATUS Status;
    BOOLEAN    IsSystemMemory;

    if (DevMode && mDeadRegions[Index].KeepInDevMode) {
      continue;
    }
    if (!XblConfigRegion (mDeadRegions[Index].Name, &Base, &Len)) {
      continue;
    }
    Pages          = (UINTN)(Len / PAGE);
    IsSystemMemory = DxeKindAt (Base, &Kind) && Kind == DXE_GCD_SYSTEM_MEMORY;
    // Raise the TPL around these calls, as the ABL did.
    Tpl = gBS->RaiseTPL (TPL_NOTIFY);
    if (IsSystemMemory) {
      Status = gBS->FreePages (Base, Pages);
    } else {
      (VOID)DxeRemove (Base, Len);
      Status = DxeAddSystemMemory (Base, Len);
    }
    if (Status == EFI_SUCCESS) {
      EFI_PHYSICAL_ADDRESS Address = Base;

      Status = gBS->AllocatePages (
        AllocateAddress, mDeadRegions[Index].Type, Pages, &Address
      );
    }
    gBS->RestoreTPL (Tpl);
    LogPrint (
      Log, "handover: release %s @0x%llx 0x%llx (%s): %s",
      mDeadRegions[Index].Name, (UINT64)Base, (UINT64)Len,
      IsSystemMemory ? "SysMem" : "MemRes->SysMem", STATUS_STR (Status)
    );
  }
}
