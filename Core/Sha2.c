/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Sha2 - SHA-256 and SHA-384. Small, not fast.
*/

#include "Core/Sha2.h"

#include "Base/Memory.h"

// SHA-512 round constants. SHA-256 uses the top 32 bits of the first 64.
STATIC CONST UINT64 mK[80] = {
  0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f,
  0xe9b5dba58189dbbc, 0x3956c25bf348b538, 0x59f111f1b605d019,
  0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242,
  0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
  0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
  0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3,
  0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65, 0x2de92c6f592b0275,
  0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
  0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f,
  0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
  0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc,
  0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
  0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6,
  0x92722c851482353b, 0xa2bfe8a14cf10364, 0xa81a664bbc423001,
  0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
  0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
  0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99,
  0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb,
  0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc,
  0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
  0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915,
  0xc67178f2e372532b, 0xca273eceea26619c, 0xd186b8c721c0c207,
  0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba,
  0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
  0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
  0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a,
  0x5fcb6fab3ad6faec, 0x6c44198c4a475817,
};

STATIC CONST UINT32 mSha256Init[8] = {
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

STATIC CONST UINT64 mSha384Init[8] = {
  0xcbbb9d5dc1059ed8, 0x629a292a367cd507, 0x9159015a3070dd17,
  0x152fecd8f70e5939, 0x67332667ffc00b31, 0x8eb44a8768581511,
  0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4,
};

STATIC inline UINT32
Rotr32 (
  IN UINT32 Value,
  IN UINT32 Bits
)
{
  return Value >> Bits | Value << (32 - Bits);
}

STATIC inline UINT64
Rotr64 (
  IN UINT64 Value,
  IN UINT32 Bits
)
{
  return Value >> Bits | Value << (64 - Bits);
}

STATIC VOID
Block256 (
  IN OUT VOID    *State,
  IN CONST UINT8 *Block
)
{
  UINT32 *H = State;
  UINT32  W[64];
  UINT32  V[8];

  for (UINTN Index = 0; Index < 16; Index++) {
    CONST UINT8 *P = Block + 4 * Index;

    W[Index] =
      (UINT32)P[0] << 24 | (UINT32)P[1] << 16 | (UINT32)P[2] << 8 | P[3];
  }
  for (UINTN Index = 16; Index < 64; Index++) {
    UINT32 S0 = Rotr32 (W[Index - 15], 7) ^ Rotr32 (W[Index - 15], 18) ^
                W[Index - 15] >> 3;
    UINT32 S1 = Rotr32 (W[Index - 2], 17) ^ Rotr32 (W[Index - 2], 19) ^
                W[Index - 2] >> 10;

    W[Index] = W[Index - 16] + S0 + W[Index - 7] + S1;
  }
  CopyMem (V, H, sizeof (V));
  for (UINTN Index = 0; Index < 64; Index++) {
    UINT32 S1 = Rotr32 (V[4], 6) ^ Rotr32 (V[4], 11) ^ Rotr32 (V[4], 25);
    UINT32 Ch = (V[4] & V[5]) ^ (~V[4] & V[6]);
    UINT32 T1 = V[7] + S1 + Ch + (UINT32)(mK[Index] >> 32) + W[Index];
    UINT32 S0 = Rotr32 (V[0], 2) ^ Rotr32 (V[0], 13) ^ Rotr32 (V[0], 22);
    UINT32 Mj = (V[0] & V[1]) ^ (V[0] & V[2]) ^ (V[1] & V[2]);

    CopyMem (V + 1, V, 7 * sizeof (*V));
    V[4] += T1;
    V[0] = T1 + S0 + Mj;
  }
  for (UINTN Index = 0; Index < 8; Index++) {
    H[Index] += V[Index];
  }
}

STATIC VOID
Block512 (
  IN OUT VOID    *State,
  IN CONST UINT8 *Block
)
{
  UINT64 *H = State;
  UINT64  W[80];
  UINT64  V[8];

  for (UINTN Index = 0; Index < 16; Index++) {
    W[Index] = 0;
    for (UINTN Byte = 0; Byte < 8; Byte++) {
      W[Index] = W[Index] << 8 | Block[8 * Index + Byte];
    }
  }
  for (UINTN Index = 16; Index < 80; Index++) {
    UINT64 S0 = Rotr64 (W[Index - 15], 1) ^ Rotr64 (W[Index - 15], 8) ^
                W[Index - 15] >> 7;
    UINT64 S1 =
      Rotr64 (W[Index - 2], 19) ^ Rotr64 (W[Index - 2], 61) ^ W[Index - 2] >> 6;

    W[Index] = W[Index - 16] + S0 + W[Index - 7] + S1;
  }
  CopyMem (V, H, sizeof (V));
  for (UINTN Index = 0; Index < 80; Index++) {
    UINT64 S1 = Rotr64 (V[4], 14) ^ Rotr64 (V[4], 18) ^ Rotr64 (V[4], 41);
    UINT64 Ch = (V[4] & V[5]) ^ (~V[4] & V[6]);
    UINT64 T1 = V[7] + S1 + Ch + mK[Index] + W[Index];
    UINT64 S0 = Rotr64 (V[0], 28) ^ Rotr64 (V[0], 34) ^ Rotr64 (V[0], 39);
    UINT64 Mj = (V[0] & V[1]) ^ (V[0] & V[2]) ^ (V[1] & V[2]);

    CopyMem (V + 1, V, 7 * sizeof (*V));
    V[4] += T1;
    V[0] = T1 + S0 + Mj;
  }
  for (UINTN Index = 0; Index < 8; Index++) {
    H[Index] += V[Index];
  }
}

// Hashes Data plus padding. LengthSize is 8 for SHA-256, 16 for SHA-384.
STATIC VOID
Run (
  IN CONST UINT8 *Data,
  IN UINTN        Size,
  IN UINTN        BlockSize,
  IN UINTN        LengthSize,
  IN VOID (*Block) (
    IN OUT VOID    *State,
    IN CONST UINT8 *Block
  ),
  IN OUT VOID *State
)
{
  UINT8  Tail[256];
  UINTN  Full = Size - Size % BlockSize;
  UINTN  Rest = Size - Full;
  UINTN  TailSize;
  UINT64 Bits = (UINT64)Size << 3;

  for (UINTN At = 0; At < Full; At += BlockSize) {
    Block (State, Data + At);
  }
  ZeroMem (Tail, sizeof (Tail));
  CopyMem (Tail, Data + Full, Rest);
  Tail[Rest] = 0x80;
  TailSize   = Rest + 1 + LengthSize <= BlockSize ? BlockSize : 2 * BlockSize;
  for (UINTN Byte = 0; Byte < 8; Byte++) {
    Tail[TailSize - 1 - Byte] = (UINT8)(Bits >> 8 * Byte);
  }
  for (UINTN At = 0; At < TailSize; At += BlockSize) {
    Block (State, Tail + At);
  }
}

VOID
Sha256 (
  IN CONST VOID *Data,
  IN UINTN       Size,
  OUT UINT8      Digest[SHA256_SIZE]
)
{
  UINT32 H[8];

  CopyMem (H, mSha256Init, sizeof (H));
  Run (Data, Size, 64, 8, Block256, H);
  for (UINTN Index = 0; Index < SHA256_SIZE; Index++) {
    Digest[Index] = (UINT8)(H[Index / 4] >> (24 - 8 * (Index % 4)));
  }
}

VOID
Sha384 (
  IN CONST VOID *Data,
  IN UINTN       Size,
  OUT UINT8      Digest[SHA384_SIZE]
)
{
  UINT64 H[8];

  CopyMem (H, mSha384Init, sizeof (H));
  Run (Data, Size, 128, 16, Block512, H);
  for (UINTN Index = 0; Index < SHA384_SIZE; Index++) {
    Digest[Index] = (UINT8)(H[Index / 8] >> (56 - 8 * (Index % 8)));
  }
}
