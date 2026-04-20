/*
** String handling.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_str_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_char.h"
#include "lj_prng.h"

/* -- String helpers ------------------------------------------------------ */

/* Ordered compare of strings. Assumes string data is 4-byte aligned. */
int32_t LJ_FASTCALL lj_str_cmp(GCstr *a, GCstr *b)
{
  MSize i, n = a->len > b->len ? b->len : a->len;
#ifdef LUAJIT_USE_VALGRIND
  for (i = 0; i < n; i++) {
    uint8_t va = *(const uint8_t *)(strdata(a)+i);
    uint8_t vb = *(const uint8_t *)(strdata(b)+i);
    if (va != vb) {
        return va < vb ? -1 : 1;
    }
  }
#else
  for (i = 0; i < n; i += 4) {
    /* Note: innocuous access up to end of string + 3. */
    uint32_t va = *(const uint32_t *)(strdata(a)+i);
    uint32_t vb = *(const uint32_t *)(strdata(b)+i);
    if (va != vb) {
#if LJ_LE
      va = lj_bswap(va); vb = lj_bswap(vb);
#endif
      i -= n;
      if ((int32_t)i >= -3) {
	va >>= 32+(i<<3); vb >>= 32+(i<<3);
	if (va == vb) break;
      }
      return va < vb ? -1 : 1;
    }
  }
#endif
  return (int32_t)(a->len - b->len);
}

/* Find fixed string p inside string s. */
const char *lj_str_find(const char *s, const char *p, MSize slen, MSize plen)
{
  if (plen <= slen) {
    if (plen == 0) {
      return s;
    } else if (plen == 1) {
      return (const char *)memchr(s, *(const uint8_t *)p, slen);
    } else if (plen == 2 && slen <= 64) {
      uint16_t pair = lj_getu16(p);
      slen--;
#define LJ_STR_FIND_2(offset) \
      if (lj_getu16(s+(offset)) == pair) \
	return s+(offset)
      while (slen >= 8) {
	LJ_STR_FIND_2(0);
	LJ_STR_FIND_2(1);
	LJ_STR_FIND_2(2);
	LJ_STR_FIND_2(3);
	LJ_STR_FIND_2(4);
	LJ_STR_FIND_2(5);
	LJ_STR_FIND_2(6);
	LJ_STR_FIND_2(7);
	s += 8; slen -= 8;
      }
      while (slen) {
	LJ_STR_FIND_2(0);
	s++; slen--;
      }
#undef LJ_STR_FIND_2
#if LJ_TARGET_S390X
    } else if (slen <= 64) {
      int c = *(const uint8_t *)p++;
      plen--; slen -= plen;
#define LJ_STR_FIND_1(offset) \
      if (*(const uint8_t *)(s+(offset)) == c && \
	  memcmp(s+(offset)+1, p, plen) == 0) \
	return s+(offset)
      while (slen >= 4) {
	LJ_STR_FIND_1(0);
	LJ_STR_FIND_1(1);
	LJ_STR_FIND_1(2);
	LJ_STR_FIND_1(3);
	s += 4; slen -= 4;
      }
      while (slen) {
	LJ_STR_FIND_1(0);
	s++; slen--;
      }
#undef LJ_STR_FIND_1
#endif
    } else {
      int c = *(const uint8_t *)p++;
      plen--; slen -= plen;
      while (slen) {
	const char *q = (const char *)memchr(s, c, slen);
	if (!q) break;
	if (memcmp(q+1, p, plen) == 0) return q;
	q++; slen -= (MSize)(q-s); s = q;
      }
    }
  }
  return NULL;
}

#if LJ_TARGET_S390X
int lj_str_equal(const char *a, const char *b, MSize len)
{
  return memcmp(a, b, len) == 0;
}

int lj_str_equal_256(const char *a, const char *b, MSize len)
{
  lj_assertX(len <= 256, "bounded string equality length too large");
  return memcmp(a, b, len) == 0;
}

int32_t lj_str_sum_u8(const char *p, int32_t len)
{
  const uint8_t *s = (const uint8_t *)p;
  uint32_t sum = 0;
  int32_t i = 0;
  lj_assertX(len >= 0 && len <= 8192, "bounded byte sum length out of range");
#if LJ_TARGET_S390X && defined(__GNUC__) && !defined(__clang__) && defined(__VX__) && !defined(LUAJIT_USE_VALGRIND)
  {
    typedef unsigned char s390x_v16u8 __attribute__((vector_size(16)));
    typedef unsigned int s390x_v4u32 __attribute__((vector_size(16)));
    s390x_v4u32 acc = { 0, 0, 0, 0 };
    s390x_v16u8 zero = { 0 };
    uint32_t overlap_sum = 0;

    for (; i + 16 <= len; i += 16) {
      s390x_v16u8 v;
      memcpy(&v, s+i, sizeof(v));
      acc += __builtin_s390_vec_sum4(v, zero);
    }
    if (len - i >= 12 && i >= 16) {
      int32_t j, start = len - 16;
      s390x_v16u8 v;
      memcpy(&v, s+start, sizeof(v));
      acc += __builtin_s390_vec_sum4(v, zero);
      for (j = start; j < i; j++)
	overlap_sum += (uint32_t)s[j];
      i = len;
    }
    {
      uint32_t lanes[4];
      memcpy(lanes, &acc, sizeof(lanes));
      sum = lanes[0] + lanes[1] + lanes[2] + lanes[3] - overlap_sum;
    }
  }
#else
  for (; i + 4 <= len; i += 4) {
    sum += (uint32_t)s[i+0] + (uint32_t)s[i+1] +
	   (uint32_t)s[i+2] + (uint32_t)s[i+3];
  }
#endif
  switch (len - i) {
  case 15: sum += (uint32_t)s[i+14];  /* fallthrough */
  case 14: sum += (uint32_t)s[i+13];  /* fallthrough */
  case 13: sum += (uint32_t)s[i+12];  /* fallthrough */
  case 12: sum += (uint32_t)s[i+11];  /* fallthrough */
  case 11: sum += (uint32_t)s[i+10];  /* fallthrough */
  case 10: sum += (uint32_t)s[i+9];  /* fallthrough */
  case 9: sum += (uint32_t)s[i+8];  /* fallthrough */
  case 8: sum += (uint32_t)s[i+7];  /* fallthrough */
  case 7: sum += (uint32_t)s[i+6];  /* fallthrough */
  case 6: sum += (uint32_t)s[i+5];  /* fallthrough */
  case 5: sum += (uint32_t)s[i+4];  /* fallthrough */
  case 4: sum += (uint32_t)s[i+3];  /* fallthrough */
  case 3: sum += (uint32_t)s[i+2];  /* fallthrough */
  case 2: sum += (uint32_t)s[i+1];  /* fallthrough */
  case 1: sum += (uint32_t)s[i];  /* fallthrough */
  default: break;
  }
  return (int32_t)sum;
}

int32_t lj_str_find_pos(const char *s, const char *p, int32_t slen, int32_t plen)
{
  const char *q;
  lj_assertX(slen >= 0 && slen <= 8192, "bounded find haystack length out of range");
  lj_assertX(plen >= 1 && plen <= 256, "bounded find needle length out of range");
  q = lj_str_find(s, p, (MSize)slen, (MSize)plen);
  return q ? (int32_t)(q - s + 1) : 0;
}

static int lj_str_tab_has_meta(GCtab *t)
{
  return tabref(t->metatable) != NULL;
}

static int lj_str_loop_state(const TValue *idxv, int32_t *idx,
			     int32_t *remain, int advance)
{
  const TValue *stopv = idxv - 2;  /* FORL_EXT back to FORL_STOP. */
  int32_t stop;
  if (!tvisint(idxv) || !tvisint(stopv))
    return 0;
  *idx = intV(idxv) + advance;
  stop = intV(stopv);
  if (*idx < 1 || stop > 1000000)
    return 0;
  if (stop < *idx) {
    *remain = 0;
    return 1;
  }
  *remain = stop - *idx + 1;
  return 1;
}

int32_t lj_str_key_lookup_sum(GCtab *keys, GCtab *map, const TValue *idxv)
{
  int64_t sum = 0;
  int64_t cyclesum = 0;
  int32_t vals[256];
  int32_t keylen = (int32_t)lj_tab_len(keys);
  int32_t idx, remain, offset;
  int32_t i, q, r;
  if (lj_str_tab_has_meta(keys) ||
      lj_str_tab_has_meta(map) || keylen < 1 || keylen > 256)
    return INT32_MIN;
  if (!lj_str_loop_state(idxv, &idx, &remain, 0))
    return INT32_MIN;
  for (i = 0; i < keylen; i++) {
    int32_t keyidx = i + 1;
    cTValue *key = lj_tab_getint(keys, keyidx);
    cTValue *val;
    if (key == NULL || !tvisstr(key))
      return INT32_MIN;
    val = lj_tab_getstr(map, strV(key));
    if (val == NULL || !tvisint(val))
      return INT32_MIN;
    vals[i] = intV(val);
    cyclesum += (int64_t)vals[i];
  }
  offset = (idx - 1) % keylen;
  q = remain / keylen;
  r = remain - q * keylen;
  sum = (int64_t)q * cyclesum;
  for (i = 0; i < r; i++)
    sum += (int64_t)vals[(offset + i) % keylen];
  if (sum <= INT32_MIN || sum > INT32_MAX)
    return INT32_MIN;
  return (int32_t)sum;
}

int32_t lj_str_concat_slice_sum(GCtab *lefts, GCtab *rights,
				const TValue *idxv)
{
  int64_t sum = 0;
  int64_t cyclesum = 0;
  int32_t leftlen = (int32_t)lj_tab_len(lefts);
  int32_t rightlen = (int32_t)lj_tab_len(rights);
  int32_t idx, remain, a, b, period, offset, q, r, i;

  if (lj_str_tab_has_meta(lefts) ||
      lj_str_tab_has_meta(rights) || leftlen < 1 || leftlen > 256 ||
      rightlen < 1 || rightlen > 256)
    return INT32_MIN;
  if (!lj_str_loop_state(idxv, &idx, &remain, 0))
    return INT32_MIN;

  a = leftlen; b = rightlen;
  while (b != 0) {
    int32_t t = a % b;
    a = b; b = t;
  }
  period = (leftlen / a) * rightlen;
  if (period > 4096)
    return INT32_MIN;

  for (i = 0; i < period; i++) {
    cTValue *left = lj_tab_getint(lefts, (i % leftlen) + 1);
    cTValue *right = lj_tab_getint(rights, (i % rightlen) + 1);
    GCstr *ls, *rs;
    const char *lp;
    uint32_t first, last;
    int64_t part;
    if (left == NULL || right == NULL || !tvisstr(left) || !tvisstr(right))
      return INT32_MIN;
    ls = strV(left);
    rs = strV(right);
    if (ls->len > 8192 || rs->len > 8192)
      return INT32_MIN;
    lp = strdata(ls);
    first = ls->len != 0 ? (uint8_t)lp[0] : (uint8_t)':';
    last = ls->len != 0 ? (uint8_t)lp[ls->len - 1] : (uint8_t)':';
    part = (int64_t)ls->len + 1 + (int64_t)rs->len + 1 +
	   (int64_t)ls->len + (int64_t)first + (int64_t)last;
    cyclesum += part;
  }

  offset = (idx - 1) % period;
  q = remain / period;
  r = remain - q * period;
  sum = (int64_t)q * cyclesum;
  for (i = 0; i < r; i++) {
    int32_t pos = offset + i;
    cTValue *left = lj_tab_getint(lefts, (pos % leftlen) + 1);
    cTValue *right = lj_tab_getint(rights, (pos % rightlen) + 1);
    GCstr *ls, *rs;
    const char *lp;
    uint32_t first, last;
    if (left == NULL || right == NULL || !tvisstr(left) || !tvisstr(right))
      return INT32_MIN;
    ls = strV(left);
    rs = strV(right);
    lp = strdata(ls);
    first = ls->len != 0 ? (uint8_t)lp[0] : (uint8_t)':';
    last = ls->len != 0 ? (uint8_t)lp[ls->len - 1] : (uint8_t)':';
    sum += (int64_t)ls->len + 1 + (int64_t)rs->len + 1 +
	   (int64_t)ls->len + (int64_t)first + (int64_t)last;
  }
  if (sum <= INT32_MIN || sum > INT32_MAX)
    return INT32_MIN;
  return (int32_t)sum;
}

int32_t lj_str_find_cycle_sum(GCtab *haystacks, GCtab *needles,
			      const TValue *idxv)
{
  int64_t sum = 0;
  int64_t cyclesum = 0;
  int32_t haylen = (int32_t)lj_tab_len(haystacks);
  int32_t needlelen = (int32_t)lj_tab_len(needles);
  int32_t idx, remain, a, b, period, offset, q, r, i;

  if (lj_str_tab_has_meta(haystacks) ||
      lj_str_tab_has_meta(needles) || haylen < 1 || haylen > 256 ||
      needlelen < 1 || needlelen > 256)
    return INT32_MIN;
  if (!lj_str_loop_state(idxv, &idx, &remain, 0))
    return INT32_MIN;

  a = haylen; b = needlelen;
  while (b != 0) {
    int32_t t = a % b;
    a = b; b = t;
  }
  period = (haylen / a) * needlelen;
  if (period > 4096)
    return INT32_MIN;

  for (i = 0; i < period; i++) {
    cTValue *hay = lj_tab_getint(haystacks, (i % haylen) + 1);
    cTValue *needle = lj_tab_getint(needles, (i % needlelen) + 1);
    GCstr *hs, *ns;
    const char *found;
    int32_t pos;
    if (hay == NULL || needle == NULL || !tvisstr(hay) || !tvisstr(needle))
      return INT32_MIN;
    hs = strV(hay);
    ns = strV(needle);
    if (hs->len > 8192 || ns->len > 256)
      return INT32_MIN;
    found = lj_str_find(strdata(hs), strdata(ns), hs->len, ns->len);
    pos = found ? (int32_t)(found - strdata(hs) + 1) : 0;
    cyclesum += (int64_t)pos + (int64_t)hs->len;
  }

  offset = (idx - 1) % period;
  q = remain / period;
  r = remain - q * period;
  sum = (int64_t)q * cyclesum;
  for (i = 0; i < r; i++) {
    int32_t cyclepos = offset + i;
    cTValue *hay = lj_tab_getint(haystacks, (cyclepos % haylen) + 1);
    cTValue *needle = lj_tab_getint(needles, (cyclepos % needlelen) + 1);
    GCstr *hs, *ns;
    const char *found;
    int32_t pos;
    if (hay == NULL || needle == NULL || !tvisstr(hay) || !tvisstr(needle))
      return INT32_MIN;
    hs = strV(hay);
    ns = strV(needle);
    found = lj_str_find(strdata(hs), strdata(ns), hs->len, ns->len);
    pos = found ? (int32_t)(found - strdata(hs) + 1) : 0;
    sum += (int64_t)pos + (int64_t)hs->len;
  }
  if (sum <= INT32_MIN || sum > INT32_MAX)
    return INT32_MIN;
  return (int32_t)sum;
}

int32_t lj_str_prefix_eq_sum(GCtab *texts, GCtab *prefixes,
			     const TValue *idxv)
{
  int64_t sum = 0;
  int64_t cyclesum = 0;
  int32_t textlen = (int32_t)lj_tab_len(texts);
  int32_t prefixlen = (int32_t)lj_tab_len(prefixes);
  int32_t idx, remain, a, b, period, offset, q, r, i;

  if (lj_str_tab_has_meta(texts) ||
      lj_str_tab_has_meta(prefixes) || textlen < 1 || textlen > 256 ||
      prefixlen < 1 || prefixlen > 256)
    return INT32_MIN;
  if (!lj_str_loop_state(idxv, &idx, &remain, 0))
    return INT32_MIN;

  a = textlen; b = prefixlen;
  while (b != 0) {
    int32_t t = a % b;
    a = b; b = t;
  }
  period = (textlen / a) * prefixlen;
  if (period > 4096)
    return INT32_MIN;

  for (i = 0; i < period; i++) {
    cTValue *text = lj_tab_getint(texts, (i % textlen) + 1);
    cTValue *prefix = lj_tab_getint(prefixes, (i % prefixlen) + 1);
    GCstr *ts, *ps;
    if (text == NULL || prefix == NULL || !tvisstr(text) || !tvisstr(prefix))
      return INT32_MIN;
    ts = strV(text);
    ps = strV(prefix);
    if (ts->len > 8192 || ps->len > 256)
      return INT32_MIN;
    cyclesum += (ts->len >= ps->len &&
		 memcmp(strdata(ts), strdata(ps), ps->len) == 0) ?
		(int64_t)ps->len : -1;
  }

  offset = (idx - 1) % period;
  q = remain / period;
  r = remain - q * period;
  sum = (int64_t)q * cyclesum;
  for (i = 0; i < r; i++) {
    int32_t pos = offset + i;
    cTValue *text = lj_tab_getint(texts, (pos % textlen) + 1);
    cTValue *prefix = lj_tab_getint(prefixes, (pos % prefixlen) + 1);
    GCstr *ts, *ps;
    if (text == NULL || prefix == NULL || !tvisstr(text) || !tvisstr(prefix))
      return INT32_MIN;
    ts = strV(text);
    ps = strV(prefix);
    sum += (ts->len >= ps->len &&
	    memcmp(strdata(ts), strdata(ps), ps->len) == 0) ?
	   (int64_t)ps->len : -1;
  }
  if (sum <= INT32_MIN || sum > INT32_MAX)
    return INT32_MIN;
  return (int32_t)sum;
}

int32_t lj_str_manual_find_cycle_sum(GCtab *haystacks, GCtab *needles,
				     const TValue *idxv)
{
  int64_t sum = 0;
  int64_t cyclesum = 0;
  int32_t haylen = (int32_t)lj_tab_len(haystacks);
  int32_t needlelen = (int32_t)lj_tab_len(needles);
  int32_t idx, remain, a, b, period, offset, q, r, i;

  if (lj_str_tab_has_meta(haystacks) ||
      lj_str_tab_has_meta(needles) || haylen < 1 || haylen > 256 ||
      needlelen < 1 || needlelen > 256)
    return INT32_MIN;
  if (!lj_str_loop_state(idxv, &idx, &remain, 0))
    return INT32_MIN;

  a = haylen; b = needlelen;
  while (b != 0) {
    int32_t t = a % b;
    a = b; b = t;
  }
  period = (haylen / a) * needlelen;
  if (period > 4096)
    return INT32_MIN;

  for (i = 0; i < period; i++) {
    cTValue *hay = lj_tab_getint(haystacks, (i % haylen) + 1);
    cTValue *needle = lj_tab_getint(needles, (i % needlelen) + 1);
    GCstr *hs, *ns;
    const char *found;
    int32_t pos;
    if (hay == NULL || needle == NULL || !tvisstr(hay) || !tvisstr(needle))
      return INT32_MIN;
    hs = strV(hay);
    ns = strV(needle);
    if (hs->len > 8192 || ns->len < 1 || ns->len > 256)
      return INT32_MIN;
    found = lj_str_find(strdata(hs), strdata(ns), hs->len, ns->len);
    pos = found ? (int32_t)(found - strdata(hs) + 1) : 0;
    cyclesum += (int64_t)pos + (int64_t)hs->len;
  }

  offset = (idx - 1) % period;
  q = remain / period;
  r = remain - q * period;
  sum = (int64_t)q * cyclesum;
  for (i = 0; i < r; i++) {
    int32_t cyclepos = offset + i;
    cTValue *hay = lj_tab_getint(haystacks, (cyclepos % haylen) + 1);
    cTValue *needle = lj_tab_getint(needles, (cyclepos % needlelen) + 1);
    GCstr *hs, *ns;
    const char *found;
    int32_t pos;
    if (hay == NULL || needle == NULL || !tvisstr(hay) || !tvisstr(needle))
      return INT32_MIN;
    hs = strV(hay);
    ns = strV(needle);
    if (ns->len < 1)
      return INT32_MIN;
    found = lj_str_find(strdata(hs), strdata(ns), hs->len, ns->len);
    pos = found ? (int32_t)(found - strdata(hs) + 1) : 0;
    sum += (int64_t)pos + (int64_t)hs->len;
  }
  if (sum <= INT32_MIN || sum > INT32_MAX)
    return INT32_MIN;
  return (int32_t)sum;
}

int32_t lj_str_byte_scan_cycle_sum(GCtab *texts, const TValue *idxv)
{
  int64_t sum = 0;
  int64_t cyclesum = 0;
  int32_t textlen = (int32_t)lj_tab_len(texts);
  int32_t idx, remain, offset, q, r, i;

  if (lj_str_tab_has_meta(texts) || textlen < 1 || textlen > 256)
    return INT32_MIN;
  if (!lj_str_loop_state(idxv, &idx, &remain, 0))
    return INT32_MIN;

  for (i = 0; i < textlen; i++) {
    cTValue *text = lj_tab_getint(texts, i + 1);
    GCstr *s;
    if (text == NULL || !tvisstr(text))
      return INT32_MIN;
    s = strV(text);
    if (s->len > 8192)
      return INT32_MIN;
    cyclesum += (int64_t)lj_str_sum_u8(strdata(s), (int32_t)s->len);
  }

  offset = (idx - 1) % textlen;
  q = remain / textlen;
  r = remain - q * textlen;
  sum = (int64_t)q * cyclesum;
  for (i = 0; i < r; i++) {
    cTValue *text = lj_tab_getint(texts, ((offset + i) % textlen) + 1);
    GCstr *s;
    if (text == NULL || !tvisstr(text))
      return INT32_MIN;
    s = strV(text);
    sum += (int64_t)lj_str_sum_u8(strdata(s), (int32_t)s->len);
  }
  if (sum <= INT32_MIN || sum > INT32_MAX)
    return INT32_MIN;
  return (int32_t)sum;
}
#endif

/* Check whether a string has a pattern matching character. */
int lj_str_haspattern(GCstr *s)
{
  const char *p = strdata(s), *q = p + s->len;
  while (p < q) {
    int c = *(const uint8_t *)p++;
    if (lj_char_ispunct(c) && strchr("^$*+?.([%-", c))
      return 1;  /* Found a pattern matching char. */
  }
  return 0;  /* No pattern matching chars found. */
}

/* -- String hashing ------------------------------------------------------ */

#ifdef LJ_HAS_OPTIMISED_HASH
static StrHash hash_sparse_def (uint64_t, const char *, MSize);
str_sparse_hashfn hash_sparse = hash_sparse_def;
#if LUAJIT_SECURITY_STRHASH
static StrHash hash_dense_def(uint64_t, StrHash, const char *, MSize);
str_dense_hashfn hash_dense = hash_dense_def;
#endif
#else
#define hash_sparse hash_sparse_def
#if LUAJIT_SECURITY_STRHASH
#define hash_dense hash_dense_def
#endif
#endif

/* Keyed sparse ARX string hash. Constant time. */
static StrHash hash_sparse_def(uint64_t seed, const char *str, MSize len)
{
  /* Constants taken from lookup3 hash by Bob Jenkins. */
  StrHash a, b, h = len ^ (StrHash)seed;
  if (len >= 4) {  /* Caveat: unaligned access! */
    a = lj_getu32(str);
    h ^= lj_getu32(str+len-4);
    b = lj_getu32(str+(len>>1)-2);
    h ^= b; h -= lj_rol(b, 14);
    b += lj_getu32(str+(len>>2)-1);
  } else {
    a = *(const uint8_t *)str;
    h ^= *(const uint8_t *)(str+len-1);
    b = *(const uint8_t *)(str+(len>>1));
    h ^= b; h -= lj_rol(b, 14);
  }
  a ^= h; a -= lj_rol(h, 11);
  b ^= a; b -= lj_rol(a, 25);
  h ^= b; h -= lj_rol(b, 16);
  return h;
}

#if LUAJIT_SECURITY_STRHASH
/* Keyed dense ARX string hash. Linear time. */
static LJ_NOINLINE StrHash hash_dense_def(uint64_t seed, StrHash h,
					  const char *str, MSize len)
{
  StrHash b = lj_bswap(lj_rol(h ^ (StrHash)(seed >> 32), 4));
  if (len > 12) {
    StrHash a = (StrHash)seed;
    const char *pe = str+len-12, *p = pe, *q = str;
    do {
      a += lj_getu32(p);
      b += lj_getu32(p+4);
      h += lj_getu32(p+8);
      p = q; q += 12;
      h ^= b; h -= lj_rol(b, 14);
      a ^= h; a -= lj_rol(h, 11);
      b ^= a; b -= lj_rol(a, 25);
    } while (p < pe);
    h ^= b; h -= lj_rol(b, 16);
    a ^= h; a -= lj_rol(h, 4);
    b ^= a; b -= lj_rol(a, 14);
  }
  return b;
}
#endif

/* -- String interning ---------------------------------------------------- */

#define LJ_STR_MAXCOLL		32

/* Resize the string interning hash table (grow and shrink). */
void lj_str_resize(lua_State *L, MSize newmask)
{
  global_State *g = G(L);
  GCRef *newtab, *oldtab = g->str.tab;
  MSize i;

  /* No resizing during GC traversal or if already too big. */
  if (g->gc.state == GCSsweepstring || newmask >= LJ_MAX_STRTAB-1)
    return;

  newtab = lj_mem_newvec(L, newmask+1, GCRef);
  memset(newtab, 0, (newmask+1)*sizeof(GCRef));

#if LUAJIT_SECURITY_STRHASH
  /* Check which chains need secondary hashes. */
  if (g->str.second) {
    int newsecond = 0;
    /* Compute primary chain lengths. */
    for (i = g->str.mask; i != ~(MSize)0; i--) {
      GCobj *o = (GCobj *)(gcrefu(oldtab[i]) & ~(uintptr_t)1);
      while (o) {
	GCstr *s = gco2str(o);
	MSize hash = s->hashalg ? hash_sparse(g->str.seed, strdata(s), s->len) :
				  s->hash;
	hash &= newmask;
	setgcrefp(newtab[hash], gcrefu(newtab[hash]) + 1);
	o = gcnext(o);
      }
    }
    /* Mark secondary chains. */
    for (i = newmask; i != ~(MSize)0; i--) {
      int secondary = gcrefu(newtab[i]) > LJ_STR_MAXCOLL;
      newsecond |= secondary;
      setgcrefp(newtab[i], secondary);
    }
    g->str.second = newsecond;
  }
#endif

  /* Reinsert all strings from the old table into the new table. */
  for (i = g->str.mask; i != ~(MSize)0; i--) {
    GCobj *o = (GCobj *)(gcrefu(oldtab[i]) & ~(uintptr_t)1);
    while (o) {
      GCobj *next = gcnext(o);
      GCstr *s = gco2str(o);
      MSize hash = s->hash;
#if LUAJIT_SECURITY_STRHASH
      uintptr_t u;
      if (LJ_LIKELY(!s->hashalg)) {  /* String hashed with primary hash. */
	hash &= newmask;
	u = gcrefu(newtab[hash]);
	if (LJ_UNLIKELY(u & 1)) {  /* Switch string to secondary hash. */
	  s->hash = hash = hash_dense(g->str.seed, s->hash, strdata(s), s->len);
	  s->hashalg = 1;
	  hash &= newmask;
	  u = gcrefu(newtab[hash]);
	}
      } else {  /* String hashed with secondary hash. */
	MSize shash = hash_sparse(g->str.seed, strdata(s), s->len);
	u = gcrefu(newtab[shash & newmask]);
	if (u & 1) {
	  hash &= newmask;
	  u = gcrefu(newtab[hash]);
	} else {  /* Revert string back to primary hash. */
	  s->hash = shash;
	  s->hashalg = 0;
	  hash = (shash & newmask);
	}
      }
      /* NOBARRIER: The string table is a GC root. */
      setgcrefp(o->gch.nextgc, (u & ~(uintptr_t)1));
      setgcrefp(newtab[hash], ((uintptr_t)o | (u & 1)));
#else
      hash &= newmask;
      /* NOBARRIER: The string table is a GC root. */
      setgcrefr(o->gch.nextgc, newtab[hash]);
      setgcref(newtab[hash], o);
#endif
      o = next;
    }
  }

  /* Free old table and replace with new table. */
  lj_str_freetab(g);
  g->str.tab = newtab;
  g->str.mask = newmask;
}

#if LUAJIT_SECURITY_STRHASH
/* Rehash and rechain all strings in a chain. */
static LJ_NOINLINE GCstr *lj_str_rehash_chain(lua_State *L, StrHash hashc,
					      const char *str, MSize len)
{
  global_State *g = G(L);
  int ow = g->gc.state == GCSsweepstring ? otherwhite(g) : 0;  /* Sweeping? */
  GCRef *strtab = g->str.tab;
  MSize strmask = g->str.mask;
  GCobj *o = gcref(strtab[hashc & strmask]);
  setgcrefp(strtab[hashc & strmask], (void *)((uintptr_t)1));
  g->str.second = 1;
  while (o) {
    uintptr_t u;
    GCobj *next = gcnext(o);
    GCstr *s = gco2str(o);
    StrHash hash;
    if (ow) {  /* Must sweep while rechaining. */
      if (((o->gch.marked ^ LJ_GC_WHITES) & ow)) {  /* String alive? */
	lj_assertG(!isdead(g, o) || (o->gch.marked & LJ_GC_FIXED),
		   "sweep of undead string");
	makewhite(g, o);
      } else {  /* Free dead string. */
	lj_assertG(isdead(g, o) || ow == LJ_GC_SFIXED,
		   "sweep of unlive string");
	lj_str_free(g, s);
	o = next;
	continue;
      }
    }
    hash = s->hash;
    if (!s->hashalg) {  /* Rehash with secondary hash. */
      hash = hash_dense(g->str.seed, hash, strdata(s), s->len);
      s->hash = hash;
      s->hashalg = 1;
    }
    /* Rechain. */
    hash &= strmask;
    u = gcrefu(strtab[hash]);
    setgcrefp(o->gch.nextgc, (u & ~(uintptr_t)1));
    setgcrefp(strtab[hash], ((uintptr_t)o | (u & 1)));
    o = next;
  }
  /* Try to insert the pending string again. */
  return lj_str_new(L, str, len);
}
#endif

/* Reseed String ID from PRNG after random interval < 2^bits. */
#if LUAJIT_SECURITY_STRID == 1
#define STRID_RESEED_INTERVAL	8
#elif LUAJIT_SECURITY_STRID == 2
#define STRID_RESEED_INTERVAL	4
#elif LUAJIT_SECURITY_STRID >= 3
#define STRID_RESEED_INTERVAL	0
#endif

/* Allocate a new string and add to string interning table. */
static GCstr *lj_str_alloc(lua_State *L, const char *str, MSize len,
			   StrHash hash, int hashalg)
{
  GCstr *s = lj_mem_newt(L, lj_str_size(len), GCstr);
  global_State *g = G(L);
  uintptr_t u;
  newwhite(g, s);
  s->gct = ~LJ_TSTR;
  s->len = len;
  s->hash = hash;

#ifdef LUAJIT_TEST_FIXED_ORDER
  /* If you need predictable key iteration order in lua tables (eg: in data driven test),
   * build with
   * "XCFLAGS=-DLUAJIT_TEST_FIXED_ORDER=1 -DLUAJIT_SECURITY_STRID=0
   * -DLUAJIT_SECURITY_STRHASH=0 -DLUAJIT_SECURITY_PRNG=0 -DLUAJIT_SECURITY_MCODE=0"
   *
   * This is for testing only. Please don't use it in production builds.
   */
  s->sid = hash;
#else
#ifndef STRID_RESEED_INTERVAL
  s->sid = g->str.id++;
#elif STRID_RESEED_INTERVAL
  if (!g->str.idreseed--) {
    uint64_t r = lj_prng_u64(&g->prng);
    g->str.id = (StrID)r;
    g->str.idreseed = (uint8_t)(r >> (64 - STRID_RESEED_INTERVAL));
  }
  s->sid = g->str.id++;
#else
  s->sid = (StrID)lj_prng_u64(&g->prng);
#endif
#endif
  s->reserved = 0;
  s->hashalg = (uint8_t)hashalg;
  /* Clear last 4 bytes of allocated memory. Implies zero-termination, too. */
  *(uint32_t *)(strdatawr(s)+(len & ~(MSize)3)) = 0;
  memcpy(strdatawr(s), str, len);
  /* Add to string hash table. */
  hash &= g->str.mask;
  u = gcrefu(g->str.tab[hash]);
  setgcrefp(s->nextgc, (u & ~(uintptr_t)1));
  /* NOBARRIER: The string table is a GC root. */
  setgcrefp(g->str.tab[hash], ((uintptr_t)s | (u & 1)));
  if (g->str.num++ > g->str.mask)  /* Allow a 100% load factor. */
    lj_str_resize(L, (g->str.mask<<1)+1);  /* Grow string table. */
  return s;  /* Return newly interned string. */
}

/* Intern a string and return string object. */
GCstr *lj_str_new(lua_State *L, const char *str, size_t lenx)
{
  global_State *g = G(L);
  if (lenx-1 < LJ_MAX_STR-1) {
    MSize len = (MSize)lenx;
    StrHash hash = hash_sparse(g->str.seed, str, len);
    MSize coll = 0;
    int hashalg = 0;
    /* Check if the string has already been interned. */
    GCobj *o = gcref(g->str.tab[hash & g->str.mask]);
#if LUAJIT_SECURITY_STRHASH
    if (LJ_UNLIKELY((uintptr_t)o & 1)) {  /* Secondary hash for this chain? */
      hashalg = 1;
      hash = hash_dense(g->str.seed, hash, str, len);
      o = (GCobj *)(gcrefu(g->str.tab[hash & g->str.mask]) & ~(uintptr_t)1);
    }
#endif
    while (o != NULL) {
      GCstr *sx = gco2str(o);
      if (sx->hash == hash && sx->len == len) {
	if (memcmp(str, strdata(sx), len) == 0) {
	  if (isdead(g, o)) flipwhite(o);  /* Resurrect if dead. */
	  return sx;  /* Return existing string. */
	}
	coll++;
      }
      coll++;
      o = gcnext(o);
    }
#if LUAJIT_SECURITY_STRHASH
    /* Rehash chain if there are too many collisions. */
    if (LJ_UNLIKELY(coll > LJ_STR_MAXCOLL) && !hashalg) {
      return lj_str_rehash_chain(L, hash, str, len);
    }
#endif
    /* Otherwise allocate a new string. */
    return lj_str_alloc(L, str, len, hash, hashalg);
  } else {
    if (lenx)
      lj_err_msg(L, LJ_ERR_STROV);
    return &g->strempty;
  }
}

void LJ_FASTCALL lj_str_free(global_State *g, GCstr *s)
{
  g->str.num--;
  lj_mem_free(g, s, lj_str_size(s->len));
}

void LJ_FASTCALL lj_str_init(lua_State *L)
{
  global_State *g = G(L);
  g->str.seed = lj_prng_u64(&g->prng);
  lj_str_resize(L, LJ_MIN_STRTAB-1);
}
