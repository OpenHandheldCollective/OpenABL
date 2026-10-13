/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Carveouts - per-SoC memory that the devicetrees keep from Linux.
*/

#include "Core/Carveouts.h"

#include "Base/Text.h"

// The entry comments come from SocCarveoutTable.h.

STATIC CONST CARVEOUTS_REGION mSm4450[] = {
  {0x80000000, 0x00600000}, // hyp@80000000
  {0x80600000, 0x00040000}, // xbl-dtlog@80600000
  {0x80640000, 0x001c0000}, // xbl-ramdump@80640000
  {0x80800000, 0x00060000}, // aop-image@80800000
  {0x80860000, 0x00020000}, // cmd-db@80860000
  {0x80880000, 0x00020000}, // aop-config@80880000
  {0x808a0000, 0x00040000}, // tme-crash-dump@808a0000
  {0x808e0000, 0x00004000}, // tme-log@808e0000
  {0x808e4000, 0x00010000}, // uefi-log@808e4000
  {0x808f4000, 0x00001000}, // chipinfo@808f4000
  {0x80900000, 0x00200000}, // smem@80900000
  {0x80b00000, 0x00100000}, // cpucp-fw@80b00000
  {0x82a00000, 0x00300000}, // wlan-msa@82a00000
  {0x84b00000, 0x00800000}, // camera@84b00000
  {0x85300000, 0x01900000}, // wpss@85300000
  {0x86c00000, 0x00700000}, // video@86c00000
  {0x87300000, 0x02100000}, // adsp@87300000
  {0x89400000, 0x00010000}, // ipa-fw@89400000
  {0x89410000, 0x0000a000}, // ipa-gsi@89410000
  {0x8941a000, 0x00002000}, // gpu-microcode@8941a000
  {0x8a000000, 0x0da00000}, // mpss@8a000000
  {0xa6e00000, 0x00040000}, // xbl-sc@a6e00000
  {0xa6f00000, 0x00100000}, // global-sync@a6f00000
  // splash_region@b8000000 = XBL Display Reserved (0xb8000000+0x02b00000),
  // excluded (OverlapsDisplayReserved keeps it)
  {0xe0600000, 0x00400000}, // cpusys-vm@e0600000
  {0xe0b00000, 0x04af3000}, // trust-ui-vm@e0b00000
  {0xe55f3000, 0x00009000}, // trust-ui-vm-qrtr@e55f3000
  {0xe55fc000, 0x00004000}, // trust-ui-vm-vblk@e55fc000
  {0xe5600000, 0x00100000}, // trust-ui-vm-swiotlb@e5600000
  {0xe8800000, 0x00100000}, // tz-stat@e8800000
  {0xe8900000, 0x00680000}, // tags@e8900000
  {0xe8f80000, 0x00500000}, // qtee@e8f80000
  {0xe9480000, 0x01200000}, // trusted-apps@e9480000
};

STATIC CONST CARVEOUTS_REGION mSm6450[] = {
  {0x80000000, 0x00600000}, // hyp_region@80000000 (stock DTB)
  {0x80600000, 0x00040000}, // xbl-dtlog@80600000
  {0x80640000, 0x001c0000}, // xbl-ramdump@80640000
  {0x80800000, 0x00060000}, // aop-image@80800000
  {0x80860000, 0x00020000}, // cmd-db@80860000
  {0x80880000, 0x00020000}, // aop-config@80880000
  {0x808a0000, 0x00040000}, // tme-crash-dump@808a0000
  {0x808e0000, 0x00004000}, // tme-log@808e0000
  {0x808e4000, 0x00010000}, // uefi-log@808e4000
  {0x80900000, 0x00200000}, // smem@80900000
  {0x80b00000, 0x00100000}, // cpucp-fw@80b00000
  {0x80c00000, 0x00001000}, // chipinfo@80c00000
  {0x80c01000, 0x00200000}, // pvm-fw@80c01000
  {0x82a00000, 0x00c00000}, // wlan-fw@82a00000
  {0x83600000, 0x00e00000}, // hyp@83600000
  {0x84b00000, 0x00800000}, // camera@84b00000
  {0x85300000, 0x01900000}, // wpss@85300000
  {0x86c00000, 0x02800000}, // adsp@86c00000
  {0x89400000, 0x00a00000}, // cdsp@89400000
  {0x89e00000, 0x00010000}, // ipa-fw@89e00000
  {0x89e10000, 0x0000a000}, // ipa-gsi@89e10000
  {0x89e1a000, 0x00002000}, // gpu-microcode@89e1a000
  {0x8bc00000, 0x0e600000}, // mpss@8bc00000
  {0x9a200000, 0x00700000}, // video@9a200000
  {0x9a900000, 0x00800000}, // hyp@9a900000
  {0xa6e00000, 0x00040000}, // xbl-sc@a6e00000
  {0xa6f00000, 0x00100000}, // global-sync@a6f00000
  {0xe0600000, 0x00400000}, // cpusys-vm@e0600000
  {0xe8800000, 0x00100000}, // tz-stat@e8800000
  {0xe8900000, 0x00680000}, // tags@e8900000
  {0xe8f80000, 0x00500000}, // qtee@e8f80000
  {0xe9480000, 0x01200000}, // trusted-apps@e9480000
};

STATIC CONST CARVEOUTS_REGION mSm8250[] = {
  {0x80000000, 0x00600000}, // memory@80000000
  {0x80700000, 0x00160000}, // memory@80700000
  {0x80860000, 0x00020000}, // memory@80860000
  {0x80900000, 0x00200000}, // memory@80900000
  {0x80b00000, 0x05300000}, // memory@80b00000
  {0x86200000, 0x00500000}, // memory@86200000
  {0x86700000, 0x00100000}, // memory@86700000
  {0x86800000, 0x00010000}, // memory@86800000
  {0x86810000, 0x0000a000}, // memory@86810000
  {0x8681a000, 0x00002000}, // memory@8681a000
  {0x86900000, 0x00500000}, // memory@86900000
  {0x86e00000, 0x00500000}, // memory@86e00000
  {0x87300000, 0x00500000}, // memory@87300000
  {0x87800000, 0x01400000}, // memory@87800000
  {0x88c00000, 0x01500000}, // memory@88c00000
  {0x8a100000, 0x01d00000}, // memory@8a100000
  {0x8be00000, 0x00100000}, // memory@8be00000
  {0x8bf00000, 0x04600000}, // memory@8bf00000
  // splash_region@9c000000 excluded
};

STATIC CONST CARVEOUTS_REGION mSm8550[] = {
  {0x80000000, 0x00a00000}, // hyp-region@80000000
  {0x80a00000, 0x00400000}, // cpusys-vm-region@80a00000
  {0x80e00000, 0x003d0000}, // hyp-tags-region@80e00000
  {0x811d0000, 0x00030000}, // hyp-tags-reserved-region@811d0000
  {0x81a00000, 0x00260000}, // xbl-dt-log-merged-region@81a00000
  {0x81c60000, 0x00020000}, // aop-cmd-db-region@81c60000
  {0x81c80000, 0x00074000}, // aop-config-merged-region@81c80000
  {0x81cf4000, 0x00001000}, // chipinfo-region@81cf4000
  {0x81d00000, 0x00200000}, // smem-region@81d00000
  {0x81f00000, 0x00020000}, // adsp-mhi-region@81f00000
  {0x82600000, 0x00100000}, // global-sync-region@82600000
  {0x82700000, 0x00100000}, // tz-stat-region@82700000
  {0x9b09a000, 0x00002000}, // gpu-micro-code-region@9b09a000
  {0x9b100000, 0x00180000}, // spss-region@9b100000
  {0x9b280000, 0x00080000}, // spu-secure-shared-memory-region@9b280000
  {0x9bb00000, 0x00700000}, // video-region@9bb00000
  {0x9c200000, 0x00700000}, // cvp-region@9c200000
  {0x9e980000, 0x00080000}, // q6-adsp-dtb-region@9e980000
  {0x9ea00000, 0x04080000}, // adspslpi-region@9ea00000
  // splash_region@b8000000 excluded
  {0xd8100000, 0x00040000}, // xbl-sc-region@d8100000
  {0xd8140000, 0x001c0000}, // cpucp-fw-region@d8140000
  {0xd8300000, 0x00500000}, // qtee-region@d8300000
  {0xe6440000, 0x002dd000}, // hwfence-shbuf-region@e6440000
  {0xfce00000, 0x02900000}, // hyp-ext-tags-region@fce00000
  {0xff700000, 0x00100000}, // hyp-ext-reserved-region@ff700000
  {0xff800000, 0x00600000}, // llcc_lpi_region@ff800000
};

STATIC CONST CARVEOUTS_REGION mSm8650[] = {
  {0x80000000, 0x00e00000}, // hyp@80000000
  {0x80e00000, 0x00400000}, // cpusys-vm@80e00000
  {0x81a00000, 0x00260000}, // xbl-dt-log-merged@81a00000
  {0x81c60000, 0x00020000}, // aop-cmd-db@81c60000
  {0x81c80000, 0x00075000}, // aop-tme-uefi-merged@81c80000
  {0x81d00000, 0x00200000}, // smem@81d00000
  {0x81f00000, 0x00020000}, // adsp-mhi@81f00000
  {0x824a0000, 0x00100000}, // pvmfw@824a0000
  {0x82600000, 0x00100000}, // global-sync@82600000
  {0x82700000, 0x00100000}, // tz-stat@82700000
  {0x82800000, 0x02000000}, // qdss@82800000
  {0x9b09a000, 0x00002000}, // gpu-micro-code@9b09a000
  {0x9b09c000, 0x00004000}, // lost-reg-mem
  {0x9b0a0000, 0x001e0000}, // spss@9b0a0000
  {0x9b280000, 0x00060000}, // spu-tz-shared@9b280000
  {0x9b2e0000, 0x00020000}, // spu-modem-shared@9b2e0000
  {0x9bb00000, 0x00800000}, // video@9bb00000
  {0x9c300000, 0x00700000}, // cvp@9c300000
  {0x9ca00000, 0x01400000}, // cdsp@9ca00000
  {0x9de00000, 0x00080000}, // q6-cdsp-dtb@9de00000
  {0x9de80000, 0x00080000}, // q6-adsp-dtb@9de80000
  {0x9df00000, 0x04080000}, // adspslpi@9df00000
  {0xd4e23000, 0x002dd000}, // hwfence-shbuf@d4e23000
  // splash-region@d5100000 excluded
  {0xd8000000, 0x00800000}, // tz-merged@d8000000
  {0xff800000, 0x00600000}, // llcc-lpi@ff800000
};

STATIC CONST CARVEOUTS_REGION mSm8750[] = {
  {0x80000000, 0x00e00000}, // gunyah-hyp@80000000
  {0x80e00000, 0x00040000}, // cpusys-vm-mem@80e00000
  {0x81200000, 0x00200000}, // cpucp@81200000
  {0x81a00000, 0x00040000}, // xbl-dtlog@81a00000
  {0x81c00000, 0x00060000}, // aop-image@81c00000
  {0x81c60000, 0x00020000}, // aop-cmd-db@81c60000
  {0x81c80000, 0x00074000}, // aop-tme-uefi-merged@81c80000
  {0x81d00000, 0x00200000}, // smem@81d00000
  {0x81f00000, 0x00100000}, // pdp-ns-shared@81f00000
  {0x82000000, 0x00380000}, // cpucp-scandump@82000000
  {0x82380000, 0x00020000}, // adsp-mhi@82380000
  {0x823a0000, 0x00040000}, // soccp-sdi@823a0000
  {0x823e0000, 0x00080000}, // pmic-minii-dump@823e0000
  {0x824a0000, 0x00100000}, // pvmfw@824a0000
  {0x82600000, 0x00100000}, // global-sync@82600000
  {0x82700000, 0x00100000}, // tz-stat@82700000
  {0x82800000, 0x02000000}, // qdss@82800000
  {0x84a00000, 0x04900000}, // dsm-partition-1@84a00000
  {0x89300000, 0x00a80000}, // dsm-partition-2@89300000
  {0x8ba00000, 0x0f600000}, // mpss@8ba00000
  {0x9b000000, 0x00080000}, // q6-mpss-dtb@9b000000
  {0x9b080000, 0x00010000}, // ipa-fw@9b080000
  {0x9b090000, 0x0000a000}, // ipa-gsi@9b090000
  {0x9b09a000, 0x00002000}, // gpu-micro-code@9b09a000
  {0x9b0a0000, 0x001e0000}, // spss@9b0a0000
  {0x9b280000, 0x00040000}, // spu-tz-shared@9b280000
  {0x9b2c0000, 0x00040000}, // spu-modem-shared@9b2c0000
  {0x9b300000, 0x00800000}, // camera@9b300000
  {0x9bb00000, 0x00800000}, // camera-2@9bb00000
  {0x9c300000, 0x00800000}, // video@9c300000
  {0x9cb00000, 0x00700000}, // cvp@9cb00000
  {0x9d200000, 0x01900000}, // cdsp@9d200000
  {0x9eb00000, 0x00080000}, // q6-cdsp-dtb@9eb00000
  {0x9ec00000, 0x00180000}, // soccp@9ec00000
  {0x9ed80000, 0x00080000}, // q6-adsp-dtb@9ed80000
  {0x9ee00000, 0x03a80000}, // adspslpi@9ee00000
  {0xb8000000, 0x001c0000}, // xbl-ramdump@b8000000
  // Display_Reserved@fc800000 excluded
  {0xd4e23000, 0x002dd000}, // hwfence-shbuf@d4e23000
  {0xd8000000, 0x00600000}, // tz-merged@d8000000
  {0xf3800000, 0x04400000}, // trust-ui-vm@f3800000
  {0xf7c00000, 0x04c00000}, // oem-vm@f7c00000
  {0xff800000, 0x00800000}, // llcc-lpi@ff800000
};

STATIC CONST CARVEOUTS_REGION mSm6115[] = {
  {0x45700000, 0x00600000}, // memory@45700000
  {0x45e00000, 0x00140000}, // memory@45e00000
  {0x45fff000, 0x00001000}, // memory@45fff000
  {0x46000000, 0x00200000}, // memory@46000000
  {0x46200000, 0x01e00000}, // memory@46200000
  {0x4ab00000, 0x06900000}, // memory@4ab00000
  {0x51400000, 0x00500000}, // memory@51400000
  {0x51900000, 0x00100000}, // memory@51900000
  {0x51a00000, 0x01e00000}, // memory@51a00000
  {0x53800000, 0x02800000}, // memory@53800000
  {0x56100000, 0x00010000}, // memory@56100000
  {0x56110000, 0x00005000}, // memory@56110000
  {0x56115000, 0x00002000}, // memory@56115000
  // memory@5c000000 + memory@5cf00000 = XBL Display Reserved carveout
  // (0x5c000000+0x01000000), excluded
  {0x60000000, 0x03900000}, // memory@60000000
  {0x89b01000, 0x00200000}, // memory@89b01000
};

STATIC CONST CARVEOUTS_REGION mSdm845[] = {
  {0x85700000, 0x00600000}, // hyp-mem@85700000
  {0x85e00000, 0x00100000}, // xbl-mem@85e00000
  {0x85fc0000, 0x00020000}, // aop-mem@85fc0000
  {0x85fe0000, 0x00020000}, // aop-cmd-db-mem@85fe0000
  {0x86000000, 0x00200000}, // smem@86000000
  {0x86200000, 0x02d00000}, // tz@86200000
  {0x88f00000, 0x00200000}, // rmtfs@88f00000
  {0x8ab00000, 0x01400000}, // qseecom@8ab00000
  {0x8bf00000, 0x00500000}, // camera-mem@8bf00000
  {0x8c400000, 0x00010000}, // ipa-fw@8c400000
  {0x8c410000, 0x00005000}, // ipa-gsi@8c410000
  {0x8c415000, 0x00002000}, // gpu@8c415000
  {0x8c500000, 0x01a00000}, // adsp@8c500000
  {0x8df00000, 0x00100000}, // wlan-msa@8df00000
  {0x8e000000, 0x07800000}, // mpss@8e000000
  {0x95800000, 0x00500000}, // venus@95800000
  {0x95d00000, 0x00800000}, // cdsp@95d00000
  {0x96500000, 0x00200000}, // mba@96500000
  {0x96700000, 0x01400000}, // slpi@96700000
  {0x97b00000, 0x00100000}, // spss@97b00000
};

#define TABLE(Array) {(Array), sizeof (Array) / sizeof (*(Array))}

CONST CARVEOUTS_TABLE gCarveoutsSm4450 = TABLE (mSm4450);
CONST CARVEOUTS_TABLE gCarveoutsSm6450 = TABLE (mSm6450);
CONST CARVEOUTS_TABLE gCarveoutsSm8250 = TABLE (mSm8250);
CONST CARVEOUTS_TABLE gCarveoutsSm8550 = TABLE (mSm8550);
CONST CARVEOUTS_TABLE gCarveoutsSm8650 = TABLE (mSm8650);
CONST CARVEOUTS_TABLE gCarveoutsSm8750 = TABLE (mSm8750);
CONST CARVEOUTS_TABLE gCarveoutsSm6115 = TABLE (mSm6115);
CONST CARVEOUTS_TABLE gCarveoutsSdm845 = TABLE (mSdm845);

CONST CARVEOUTS_TABLE *
CarveoutsForChip (
  IN UINT32       ChipId,
  IN CONST CHAR8 *Name OPTIONAL
)
{
  STATIC CONST struct {
    CONST CHAR8           *Prefix;
    CONST CARVEOUTS_TABLE *Table;
  } ByName[] = {
    {"SM4450", &gCarveoutsSm4450}, {"SM6450", &gCarveoutsSm6450},
    {"SM8250", &gCarveoutsSm8250}, {"SM8550", &gCarveoutsSm8550},
    {"SM8650", &gCarveoutsSm8650}, {"SM8750", &gCarveoutsSm8750},
    {"SM6115", &gCarveoutsSm6115}, {"SDM845", &gCarveoutsSdm845},
    {"SDA845", &gCarveoutsSdm845},
  };
  OABL_TEXT Chip;

  switch (ChipId) {
  case 654:
    return &gCarveoutsSm4450;
  case 634:
    return &gCarveoutsSm6450;
  case 356:
    return &gCarveoutsSm8250;
  case 601:
  case 603:
    return &gCarveoutsSm8550;
  case 682:
    return &gCarveoutsSm8650;
  case 639:
  case 706:
    return &gCarveoutsSm8750;
  case 444:
  case 467:
    return &gCarveoutsSm6115;
  case 321:
  case 341:
    return &gCarveoutsSdm845;
  }
  Chip = TextTrim (TextOf (Name));
  for (UINTN Index = 0; Index < sizeof (ByName) / sizeof (*ByName); Index++) {
    if (TextStartsWith (Chip, ByName[Index].Prefix)) {
      return ByName[Index].Table;
    }
  }
  return NULL;
}

// XBL's splash framebuffer (Display Reserved) is never handed over, since
// the panel may still be scanning it out.
STATIC BOOLEAN
OnDisplay (
  IN UINT64                  Start,
  IN UINT64                  End,
  IN CONST CARVEOUTS_REGION *Display OPTIONAL
)
{
  return Display && Display->Len != 0 && Start < Display->Base + Display->Len &&
         End > Display->Base;
}

// Appends the GCD spans inside [From, To) that Take accepts to Out, except
// those on the display.
STATIC BOOLEAN
SpansIn (
  IN CONST CARVEOUTS_GCD    *Gcd,
  IN UINTN                   GcdCount,
  IN UINT64                  From,
  IN UINT64                  To,
  IN CONST CARVEOUTS_REGION *Display OPTIONAL,
  IN BOOLEAN (*Take) (IN CONST CARVEOUTS_GCD *Descriptor),
  IN OUT CARVEOUTS_SPANS *Out
)
{
  for (UINTN Index = 0; Index < GcdCount; Index++) {
    CONST CARVEOUTS_GCD *Descriptor = &Gcd[Index];
    UINT64               Start;
    UINT64               End;

    if (!Take (Descriptor)) {
      continue;
    }
    Start = Descriptor->Base > From ? Descriptor->Base : From;
    End   = Descriptor->Base + Descriptor->Len < To
              ? Descriptor->Base + Descriptor->Len
              : To;
    if (Start >= End || OnDisplay (Start, End, Display)) {
      continue;
    }
    if (!VecPush (
          Out, (CARVEOUTS_SPAN){Start, End,
                                Descriptor->Kind == CARVEOUTS_GCD_RESERVED}
        )) {
      return FALSE;
    }
  }
  return TRUE;
}

STATIC BOOLEAN
HoleOrReserved (IN CONST CARVEOUTS_GCD *Descriptor)
{
  return Descriptor->Kind == CARVEOUTS_GCD_NON_EXISTENT ||
         Descriptor->Kind == CARVEOUTS_GCD_RESERVED;
}

BOOLEAN
CarveoutsSpansFromTable (
  IN CONST CARVEOUTS_REGION *Banks,
  IN UINTN                   BankCount,
  IN CONST CARVEOUTS_REGION *Nomap,
  IN UINTN                   NomapCount,
  IN CONST CARVEOUTS_GCD    *Gcd,
  IN UINTN                   GcdCount,
  IN CONST CARVEOUTS_REGION *Display OPTIONAL,
  OUT CARVEOUTS_SPANS       *Out
)
{
  *Out = (CARVEOUTS_SPANS){0};
  for (UINTN Bank = 0; Bank < BankCount; Bank++) {
    UINT64 BankEnd = Banks[Bank].Base + Banks[Bank].Len;
    UINT64 Cursor  = Banks[Bank].Base;

    for (UINTN Index = 0; Index < NomapCount; Index++) {
      UINT64 NomapBase = Nomap[Index].Base;
      UINT64 NomapEnd  = Nomap[Index].Base + Nomap[Index].Len;

      if (NomapEnd <= Cursor) {
        continue;
      }
      if (NomapBase >= BankEnd) {
        break;
      }
      if (NomapBase > Cursor &&
          !SpansIn (
            Gcd, GcdCount, Cursor, NomapBase < BankEnd ? NomapBase : BankEnd,
            Display, HoleOrReserved, Out
          )) {
        goto OutOfMemory;
      }
      if (NomapEnd > Cursor) {
        Cursor = NomapEnd;
      }
      if (Cursor >= BankEnd) {
        break;
      }
    }
    if (Cursor < BankEnd &&
        !SpansIn (
          Gcd, GcdCount, Cursor, BankEnd, Display, HoleOrReserved, Out
        )) {
      goto OutOfMemory;
    }
  }
  return TRUE;
OutOfMemory:
  VecFree (Out);
  return FALSE;
}

STATIC BOOLEAN
HoleOrWriteBack (IN CONST CARVEOUTS_GCD *Descriptor)
{
  return Descriptor->Kind == CARVEOUTS_GCD_NON_EXISTENT ||
         (Descriptor->Kind == CARVEOUTS_GCD_RESERVED && Descriptor->WriteBack);
}

BOOLEAN
CarveoutsSpansFromBanks (
  IN CONST CARVEOUTS_REGION *Banks,
  IN UINTN                   BankCount,
  IN CONST CARVEOUTS_GCD    *Gcd,
  IN UINTN                   GcdCount,
  IN CONST CARVEOUTS_REGION *Display OPTIONAL,
  OUT CARVEOUTS_SPANS       *Out
)
{
  *Out = (CARVEOUTS_SPANS){0};
  for (UINTN Bank = 0; Bank < BankCount; Bank++) {
    if (!SpansIn (
          Gcd, GcdCount, Banks[Bank].Base, Banks[Bank].Base + Banks[Bank].Len,
          Display, HoleOrWriteBack, Out
        )) {
      VecFree (Out);
      return FALSE;
    }
  }
  return TRUE;
}
