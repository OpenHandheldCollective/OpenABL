# Licences

OpenABL is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. The full text is in `LICENSE`.

Copyright (C) 2026-present Open Handheld Collective
(https://github.com/OpenHandheldCollective)

Each file names its licence in an `SPDX-License-Identifier` line, and the
licence texts are in `LICENSES/`. Everything written for OpenABL is
GPL-3.0-or-later, with two differences:

- `Abl/`: the files and our changes in `openabl.patch` are built into
  Qualcomm's LinuxLoader. On SM4450, SM6450, SM7675, SM8550, SM8650 and
  SM8750 it links BoringSSL code under the OpenSSL and SSLeay licences, which
  the GPL alone doesn't allow, so these add the permission in
  `LICENSES/GPL-3.0-linking-source-exception.txt`. It allows that combination
  only and still requires its source. The edk2 changes the patch backports
  stay BSD-2-Clause-Patent, and the code the patch changes keeps its licence.
- `Linux/`: the kernel patch is GPL-2.0-only, the Linux kernel's licence.
  The patch in `Linux/minkipc/` is BSD-3-Clause-Clear, the licence of
  qualcomm/minkipc, which it changes.

## Third-party parts in this repository

| Where | What | Licence | Copyright |
|---|---|---|---|
| `Core/SysFontGlyphs.c` | 8x19 glyphs from edk2 GraphicsConsoleDxe (`LaffStd.c`) | BSD-2-Clause-Patent | Copyright (c) 2006 - 2008, Intel Corporation |
| `Efi/Abl.c` | 5x12 font (`font5x12.h`) | BSD-2-Clause | Copyright (C) 2008 The Android Open Source Project |
| `Gfx/Icons.c` | icon designs after Tabler Icons | MIT | Copyright (c) Paweł Kuna |
| `Gfx/Raster.c` | rasteriser ported from font-rs and ab_glyph_rasterizer | Apache-2.0 | Copyright 2015 Google Inc.; Copyright (C) 2020 Alex Butler |
| `Core/LibFdt.h` | libfdt declarations | GPL-2.0-or-later OR BSD-2-Clause | Copyright (C) 2006 David Gibson, IBM Corporation; Copyright 2012 Kim Phillips, Freescale Semiconductor |
| `Assets/montserrat.oablf` | Montserrat, converted | OFL-1.1 (`Assets/Montserrat-OFL.txt`) | Copyright 2011 The Montserrat Project Authors |
| `Assets/SourceHanSansSC-Normal.otf` | Source Han Sans SC Normal 2.005, unmodified; the build subsets and converts it to `cjk-sc.oablf`, which is not named with the Reserved Font Name | OFL-1.1 (`Assets/SourceHanSans-OFL.txt`) | Copyright 2014-2025 Adobe |

## Fetched at build time

`Scripts/fetch.sh` downloads these into `build/`. They are not in this
repository, but a built image contains them.

| Component | Built into | Licence | Copyright |
|---|---|---|---|
| microui | the menu program | MIT | Copyright (c) 2024 rxi |
| edk2 packages of Qualcomm's ABL (`KERNEL.PLATFORM.3.0.r23-02400-kernel.0`) | the ABL | BSD-2-Clause-Patent | Intel Corporation, ARM Ltd and others |
| QcomModulePkg | the ABL | BSD-3-Clause, BSD-3-Clause-Clear | The Linux Foundation; Qualcomm Innovation Center, Inc.; Google Inc. |
| FdtLib (libfdt) | the ABL | GPL-2.0-or-later OR BSD-2-Clause | David Gibson, IBM Corporation and others |
| LibUfdt (libufdt) | the ABL | Apache-2.0; `ufdt_overlay.c` BSD-2-Clause | The Android Open Source Project; Oleksandr Tymoshenko |
| libavb | the ABL | MIT | Copyright (C) 2016 The Android Open Source Project |
| zlib | the ABL | Zlib | Copyright (C) 1995-2013 Jean-loup Gailly and Mark Adler |
| LZ4 (`lib/`) | the ABL | BSD-2-Clause | Copyright (C) 2011-2020 Yann Collet |
| open-dice | the ABL on the six chipsets above | Apache-2.0 | Copyright 2020 Google LLC |
| BoringSSL: SHA-512, HMAC, digest | the ABL on the six chipsets above | OpenSSL and SSLeay (`LICENSES/BoringSSL.txt`) | The OpenSSL Project; Eric Young |
| BoringSSL: HKDF, Curve25519 with fiat-crypto | the ABL on the six chipsets above | ISC; MIT (`LICENSES/BoringSSL.txt`) | Google Inc.; the fiat-crypto authors |
| qtestsign | signs the image; not in it | GPL-2.0-only | |

## Distributing an image

A built image holds the menu program and Qualcomm's ABL with the components
above, and is distributed under GPL-3.0-or-later as a whole. Whoever
distributes one must offer its complete source: this repository at that
revision and the sources `Scripts/fetch.sh` downloads. They must also pass on
`LICENSE`, `LICENSES/`, the font licences in `Assets/` and the copyright
notices above. The BoringSSL code also requires: "This product includes
software developed by the OpenSSL Project for use in the OpenSSL Toolkit
(http://www.openssl.org/)" and "This product includes cryptographic software
written by Eric Young (eay@cryptsoft.com)".
