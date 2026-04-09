/*
** Experimental s390x string hash dispatch.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
*/

#include "lj_arch.h"

#if LJ_TARGET_S390X && defined(LJ_HAS_OPTIMISED_HASH)

#include <string.h>

#include "lj_def.h"
#include "lj_str.h"

static const uint64_t HASH_MIX_K1 = U64x(9e3779b9, 7f4a7c15);
static const uint64_t HASH_MIX_K2 = U64x(c2b2ae3d, 27d4eb4f);
static const uint64_t HASH_MIX_K3 = U64x(165667b1, 9e3779f9);

static LJ_AINLINE uint64_t s390x_hash_load_u64(const char *str)
{
  return *(const uint64_t *)(const void *)str;
}

static LJ_AINLINE uint32_t s390x_hash_load_u32(const char *str)
{
  return *(const uint32_t *)(const void *)str;
}

static LJ_AINLINE uint64_t s390x_hash_avalanche(uint64_t h)
{
  h ^= h >> 33;
  h *= HASH_MIX_K2;
  h ^= h >> 29;
  h *= HASH_MIX_K3;
  h ^= h >> 32;
  return h;
}

static LJ_AINLINE MSize s390x_hash_sample_offset(MSize len, MSize desired)
{
  return desired > len - 8 ? len - 8 : desired;
}

static StrHash hash_sparse_s390x_mix64(uint64_t seed, const char *str, MSize len)
{
  uint64_t h = seed ^ ((uint64_t)len * HASH_MIX_K1);

  if (len < 4) {
    uint32_t a, b;
    StrHash x = len ^ (StrHash)seed;
    a = *(const uint8_t *)str;
    x ^= *(const uint8_t *)(str + len - 1);
    b = *(const uint8_t *)(str + (len >> 1));
    x ^= b;
    x -= lj_rol(b, 14);
    a ^= x;
    a -= lj_rol(x, 11);
    b ^= a;
    b -= lj_rol(a, 25);
    x ^= b;
    x -= lj_rol(b, 16);
    return x;
  }

  if (len < 8) {
    uint32_t a = s390x_hash_load_u32(str);
    uint32_t b = s390x_hash_load_u32(str + len - 4);
    uint32_t m = s390x_hash_load_u32(str + ((len >> 1) - 2));
    h ^= ((uint64_t)a << 32) ^ b;
    h += lj_rol((uint64_t)m, 17);
    return (StrHash)(s390x_hash_avalanche(h) ^ (s390x_hash_avalanche(h) >> 32));
  }

  {
    MSize last = len - 8;
    MSize mid = s390x_hash_sample_offset(len, (len >> 1) - 4);
    MSize q3 = s390x_hash_sample_offset(len, ((len * 3) >> 2) - 4);
    uint64_t head = s390x_hash_load_u64(str);
    uint64_t tail = s390x_hash_load_u64(str + last);
    uint64_t middle = s390x_hash_load_u64(str + mid);
    uint64_t upper = s390x_hash_load_u64(str + q3);
    h ^= lj_rol(head, 11);
    h += HASH_MIX_K2;
    h ^= lj_rol(tail, 17);
    h *= HASH_MIX_K3;
    h ^= lj_rol(middle, 29);
    h += lj_rol(upper, 37);
  }

  h = s390x_hash_avalanche(h);
  return (StrHash)(h ^ (h >> 32));
}

void str_hash_init_s390x(void)
{
  const char *mode = getenv("LUAJIT_S390X_HASH_MODE");
  if (!mode || mode[0] == '\0' || strcmp(mode, "auto") == 0 ||
      strcmp(mode, "generic") == 0)
    return;
  if (strcmp(mode, "mix64") == 0)
    hash_sparse = hash_sparse_s390x_mix64;
}

#endif
