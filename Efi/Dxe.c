/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Dxe - GCD memory space calls from the DXE services table (PI spec, vol. 2).
*/

#include "Efi/Dxe.h"

#include <Guid/DxeServices.h>

// Finds the DXE services table. It is only valid while boot services run.
STATIC DXE_SERVICES *
ServicesTable (VOID)
{
  for (UINTN Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    EFI_CONFIGURATION_TABLE *Entry = &gST->ConfigurationTable[Index];
    if (CompareGuid (&Entry->VendorGuid, &gEfiDxeServicesTableGuid)) {
      return Entry->VendorTable;
    }
  }
  return NULL;
}

BOOLEAN
DxeMemorySpaceMap (OUT DXE_GCD_MAP *Out)
{
  DXE_SERVICES                    *Services = ServicesTable ();
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR *Map      = NULL;
  UINTN                            Count    = 0;
  EFI_STATUS                       Status;

  *Out = (DXE_GCD_MAP){0};
  if (!Services) {
    return FALSE;
  }
  // GetMemorySpaceMap allocates the map from pool. We free it.
  Status = Services->GetMemorySpaceMap (&Count, &Map);
  if (Status != EFI_SUCCESS || !Map) {
    return FALSE;
  }
  if (!VecReserve (Out, Count)) {
    gBS->FreePool (Map);
    return FALSE;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    CONST EFI_GCD_MEMORY_SPACE_DESCRIPTOR *Descriptor = &Map[Index];
    CARVEOUTS_GCD_KIND                     Kind;

    switch (Descriptor->GcdMemoryType) {
    case EfiGcdMemoryTypeNonExistent:
      Kind = CARVEOUTS_GCD_NON_EXISTENT;
      break;
    case EfiGcdMemoryTypeReserved:
      Kind = CARVEOUTS_GCD_RESERVED;
      break;
    default:
      Kind = CARVEOUTS_GCD_OTHER;
      break;
    }
    Out->Items[Out->Count++] = (CARVEOUTS_GCD){
      .Base      = Descriptor->BaseAddress,
      .Len       = Descriptor->Length,
      .Kind      = Kind,
      .WriteBack = (Descriptor->Capabilities & EFI_MEMORY_WB) != 0,
    };
  }
  gBS->FreePool (Map);
  return TRUE;
}

BOOLEAN
DxeDescriptor (
  IN UINT64                            Address,
  OUT EFI_GCD_MEMORY_SPACE_DESCRIPTOR *Out
)
{
  DXE_SERVICES *Services = ServicesTable ();

  *Out = (EFI_GCD_MEMORY_SPACE_DESCRIPTOR){0};
  return Services &&
         Services->GetMemorySpaceDescriptor (Address, Out) == EFI_SUCCESS;
}

BOOLEAN
DxeKindAt (
  IN UINT64   Address,
  OUT UINT32 *Kind
)
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Descriptor;

  if (!DxeDescriptor (Address, &Descriptor)) {
    return FALSE;
  }
  *Kind = (UINT32)Descriptor.GcdMemoryType;
  return TRUE;
}

EFI_STATUS
DxeSetAttributes (
  IN UINT64 Base,
  IN UINT64 Length,
  IN UINT64 Attributes
)
{
  DXE_SERVICES *Services = ServicesTable ();

  return Services
           ? Services->SetMemorySpaceAttributes (Base, Length, Attributes)
           : EFI_UNSUPPORTED;
}

EFI_STATUS
DxeAddSystemMemory (
  IN UINT64 Base,
  IN UINT64 Length
)
{
  DXE_SERVICES *Services = ServicesTable ();

  return Services
           ? Services->AddMemorySpace (
               EfiGcdMemoryTypeSystemMemory, Base, Length, DXE_ALL_CACHING
             )
           : EFI_UNSUPPORTED;
}

EFI_STATUS
DxeRemove (
  IN UINT64 Base,
  IN UINT64 Length
)
{
  DXE_SERVICES *Services = ServicesTable ();

  return Services ? Services->RemoveMemorySpace (Base, Length)
                  : EFI_UNSUPPORTED;
}
