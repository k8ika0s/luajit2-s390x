/*
** Experimental s390x text helper dispatch.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_s390x_text_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_char.h"
#include "lj_str.h"
#include "lj_s390x_text.h"

#if LJ_TARGET_S390X

typedef enum S390XTextMode {
  S390X_TEXT_GENERIC,
  S390X_TEXT_LIBC,
  S390X_TEXT_SCAN2,
  S390X_TEXT_BSWAP64,
  S390X_TEXT_ASCII8,
  S390X_TEXT_SPAN8
} S390XTextMode;

static int s390x_text_cmp_mode = -1;
static int s390x_text_find_mode = -1;
static int s390x_text_transform_mode = -1;
static int s390x_text_pattern_mode = -1;

#if LJ_TARGET_LINUX
extern void *memmem(const void *haystack, size_t haystacklen,
		    const void *needle, size_t needlelen);
#endif

static int s390x_text_parse_mode(const char *mode, int default_mode)
{
  if (!mode || mode[0] == '\0' || strcmp(mode, "auto") == 0)
    return default_mode;
  if (strcmp(mode, "libc") == 0)
    return S390X_TEXT_LIBC;
  if (strcmp(mode, "scan2") == 0)
    return S390X_TEXT_SCAN2;
  if (strcmp(mode, "bswap64") == 0)
    return S390X_TEXT_BSWAP64;
  if (strcmp(mode, "ascii8") == 0)
    return S390X_TEXT_ASCII8;
  if (strcmp(mode, "span8") == 0)
    return S390X_TEXT_SPAN8;
  return S390X_TEXT_GENERIC;
}

static int s390x_text_resolve_mode(const char *override_name, int default_mode,
				   int *slot)
{
  if (*slot < 0) {
    const char *mode = getenv(override_name);
    if (!mode || mode[0] == '\0')
      mode = getenv("LUAJIT_S390X_TEXT_MODE");
    *slot = s390x_text_parse_mode(mode, default_mode);
  }
  return *slot;
}

int lj_s390x_text_cmp_active(void)
{
  return s390x_text_resolve_mode("LUAJIT_S390X_TEXT_COMPARE_MODE",
				 S390X_TEXT_GENERIC, &s390x_text_cmp_mode) !=
	 S390X_TEXT_GENERIC;
}

int lj_s390x_text_find_active(void)
{
  return s390x_text_resolve_mode("LUAJIT_S390X_TEXT_FIND_MODE",
				 S390X_TEXT_GENERIC, &s390x_text_find_mode) !=
	 S390X_TEXT_GENERIC;
}

int lj_s390x_text_transform_active(void)
{
  return s390x_text_resolve_mode("LUAJIT_S390X_TEXT_TRANSFORM_MODE",
				 S390X_TEXT_GENERIC,
				 &s390x_text_transform_mode) !=
	 S390X_TEXT_GENERIC;
}

int lj_s390x_text_pattern_active(void)
{
  return s390x_text_resolve_mode("LUAJIT_S390X_TEXT_PATTERN_MODE",
				 S390X_TEXT_GENERIC,
				 &s390x_text_pattern_mode) !=
	 S390X_TEXT_GENERIC;
}

int32_t LJ_FASTCALL lj_s390x_text_str_cmp(GCstr *a, GCstr *b)
{
  MSize n = a->len > b->len ? b->len : a->len;
  int32_t diff = (int32_t)(a->len - b->len);
  int res = memcmp(strdata(a), strdata(b), n);
  if (res == 0)
    return diff;
  return res < 0 ? -1 : 1;
}

static const char *s390x_text_find_fallback(const char *s, const char *p,
					    MSize slen, MSize plen)
{
  if (plen <= slen) {
    if (plen == 0) {
      return s;
    } else {
      int c = *(const uint8_t *)p++;
      plen--;
      slen -= plen;
      while (slen) {
	const char *q = (const char *)memchr(s, c, slen);
	if (!q) break;
	if (memcmp(q+1, p, plen) == 0) return q;
	q++;
	slen -= (MSize)(q-s);
	s = q;
      }
    }
  }
  return NULL;
}

static const char *s390x_text_find_scan2(const char *s, const char *p,
					 MSize slen, MSize plen)
{
  MSize remaining = slen - plen + 1;
  uint8_t first = *(const uint8_t *)p;
  uint8_t last = *(const uint8_t *)(p + plen - 1);
  const char *mid = p + 1;
  MSize midlen = plen - 2;

  while (remaining) {
    const char *q = (const char *)memchr(s, first, remaining);
    if (!q)
      return NULL;
    if (*(const uint8_t *)(q + plen - 1) == last &&
	(midlen == 0 || memcmp(q + 1, mid, midlen) == 0))
      return q;
    q++;
    remaining -= (MSize)(q - s);
    s = q;
  }
  return NULL;
}

const char *lj_s390x_text_find(const char *s, const char *p, MSize slen, MSize plen)
{
  int mode = s390x_text_resolve_mode("LUAJIT_S390X_TEXT_FIND_MODE",
				     S390X_TEXT_GENERIC, &s390x_text_find_mode);
  if (plen == 0)
    return s;
  if (plen > slen)
    return NULL;
  if (plen == 1)
    return (const char *)memchr(s, *(const uint8_t *)p, slen);
  if (mode == S390X_TEXT_SCAN2)
    return s390x_text_find_scan2(s, p, slen, plen);
#if LJ_TARGET_LINUX
  if (mode == S390X_TEXT_LIBC) {
    void *q = memmem(s, (size_t)slen, p, (size_t)plen);
    if (q)
      return (const char *)q;
  }
#endif
  return s390x_text_find_fallback(s, p, slen, plen);
}

static LJ_AINLINE uint8_t s390x_text_ascii_lower_byte(uint8_t c)
{
  return (uint8_t)(c + ((c >= 'A' && c <= 'Z') ? 0x20 : 0));
}

static LJ_AINLINE uint8_t s390x_text_ascii_upper_byte(uint8_t c)
{
  return (uint8_t)(c - ((c >= 'a' && c <= 'z') ? 0x20 : 0));
}

static char *s390x_text_copy_lower_ascii8(char *w, const char *q, MSize len)
{
  while (len >= 8) {
    w[0] = (char)s390x_text_ascii_lower_byte((uint8_t)q[0]);
    w[1] = (char)s390x_text_ascii_lower_byte((uint8_t)q[1]);
    w[2] = (char)s390x_text_ascii_lower_byte((uint8_t)q[2]);
    w[3] = (char)s390x_text_ascii_lower_byte((uint8_t)q[3]);
    w[4] = (char)s390x_text_ascii_lower_byte((uint8_t)q[4]);
    w[5] = (char)s390x_text_ascii_lower_byte((uint8_t)q[5]);
    w[6] = (char)s390x_text_ascii_lower_byte((uint8_t)q[6]);
    w[7] = (char)s390x_text_ascii_lower_byte((uint8_t)q[7]);
    q += 8;
    w += 8;
    len -= 8;
  }
  while (len) {
    *w++ = (char)s390x_text_ascii_lower_byte((uint8_t)*q++);
    len--;
  }
  return w;
}

static char *s390x_text_copy_upper_ascii8(char *w, const char *q, MSize len)
{
  while (len >= 8) {
    w[0] = (char)s390x_text_ascii_upper_byte((uint8_t)q[0]);
    w[1] = (char)s390x_text_ascii_upper_byte((uint8_t)q[1]);
    w[2] = (char)s390x_text_ascii_upper_byte((uint8_t)q[2]);
    w[3] = (char)s390x_text_ascii_upper_byte((uint8_t)q[3]);
    w[4] = (char)s390x_text_ascii_upper_byte((uint8_t)q[4]);
    w[5] = (char)s390x_text_ascii_upper_byte((uint8_t)q[5]);
    w[6] = (char)s390x_text_ascii_upper_byte((uint8_t)q[6]);
    w[7] = (char)s390x_text_ascii_upper_byte((uint8_t)q[7]);
    q += 8;
    w += 8;
    len -= 8;
  }
  while (len) {
    *w++ = (char)s390x_text_ascii_upper_byte((uint8_t)*q++);
    len--;
  }
  return w;
}

static LJ_AINLINE int s390x_text_match_class_ascii(uint8_t c, int cl)
{
  int lower = (cl & 0x20);
  int match = 0;
  switch (cl | 0x20) {
  case 'a':
    match = ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
    break;
  case 'd':
    match = (c >= '0' && c <= '9');
    break;
  case 'l':
    match = (c >= 'a' && c <= 'z');
    break;
  case 's':
    match = lj_char_isspace(c);
    break;
  case 'u':
    match = (c >= 'A' && c <= 'Z');
    break;
  case 'w':
    match = ((c >= '0' && c <= '9') ||
	     (c >= 'A' && c <= 'Z') ||
	     (c >= 'a' && c <= 'z'));
    break;
  case 'x':
    match = ((c >= '0' && c <= '9') ||
	     (c >= 'A' && c <= 'F') ||
	     (c >= 'a' && c <= 'f'));
    break;
  default:
    return -1;
  }
  return lower ? match : !match;
}

static LJ_AINLINE int s390x_text_class_supported(int cl)
{
  return s390x_text_match_class_ascii((uint8_t)'A', cl) >= 0;
}

MSize lj_s390x_text_pattern_span(const char *s, const char *end, int cl)
{
  int mode = s390x_text_resolve_mode("LUAJIT_S390X_TEXT_PATTERN_MODE",
				     S390X_TEXT_GENERIC,
				     &s390x_text_pattern_mode);
  const char *p = s;
  if (mode != S390X_TEXT_SPAN8)
    return ~(MSize)0;
  if (!s390x_text_class_supported(cl))
    return ~(MSize)0;
  while ((end - p) >= 8) {
    if (!s390x_text_match_class_ascii((uint8_t)p[0], cl) ||
	!s390x_text_match_class_ascii((uint8_t)p[1], cl) ||
	!s390x_text_match_class_ascii((uint8_t)p[2], cl) ||
	!s390x_text_match_class_ascii((uint8_t)p[3], cl) ||
	!s390x_text_match_class_ascii((uint8_t)p[4], cl) ||
	!s390x_text_match_class_ascii((uint8_t)p[5], cl) ||
	!s390x_text_match_class_ascii((uint8_t)p[6], cl) ||
	!s390x_text_match_class_ascii((uint8_t)p[7], cl))
      break;
    p += 8;
  }
  while (p < end && s390x_text_match_class_ascii((uint8_t)*p, cl))
    p++;
  return (MSize)(p - s);
}

MSize lj_s390x_text_pattern_seek(const char *s, const char *end, int cl)
{
  int mode = s390x_text_resolve_mode("LUAJIT_S390X_TEXT_PATTERN_MODE",
				     S390X_TEXT_GENERIC,
				     &s390x_text_pattern_mode);
  const char *p = s;
  if (mode != S390X_TEXT_SPAN8)
    return ~(MSize)0;
  if (!s390x_text_class_supported(cl))
    return ~(MSize)0;
  while ((end - p) >= 8) {
    if (s390x_text_match_class_ascii((uint8_t)p[0], cl) ||
	s390x_text_match_class_ascii((uint8_t)p[1], cl) ||
	s390x_text_match_class_ascii((uint8_t)p[2], cl) ||
	s390x_text_match_class_ascii((uint8_t)p[3], cl) ||
	s390x_text_match_class_ascii((uint8_t)p[4], cl) ||
	s390x_text_match_class_ascii((uint8_t)p[5], cl) ||
	s390x_text_match_class_ascii((uint8_t)p[6], cl) ||
	s390x_text_match_class_ascii((uint8_t)p[7], cl))
      break;
    p += 8;
  }
  while (p < end && !s390x_text_match_class_ascii((uint8_t)*p, cl))
    p++;
  return (MSize)(p - s);
}

char *lj_s390x_text_copy_reverse(char *w, const char *q, MSize len)
{
  int mode = s390x_text_resolve_mode("LUAJIT_S390X_TEXT_TRANSFORM_MODE",
				     S390X_TEXT_GENERIC,
				     &s390x_text_transform_mode);
  const char *p = q + len;
  if (mode == S390X_TEXT_BSWAP64) {
    while (len >= 8) {
      uint64_t x;
      p -= 8;
      x = *(const uint64_t *)(const void *)p;
      *(uint64_t *)(void *)w = lj_bswap64(x);
      w += 8;
      len -= 8;
    }
  }
  if (len == 0)
    return w;
  p--;
  char *e = w + len;
  while (w < e)
    *w++ = *p--;
  return w;
}

char *lj_s390x_text_copy_lower(char *w, const char *q, MSize len)
{
  int mode = s390x_text_resolve_mode("LUAJIT_S390X_TEXT_TRANSFORM_MODE",
				     S390X_TEXT_GENERIC,
				     &s390x_text_transform_mode);
  if (mode == S390X_TEXT_ASCII8)
    return s390x_text_copy_lower_ascii8(w, q, len);
  char *e = w + len;
  for (; w < e; w++, q++) {
    uint32_t c = *(const uint8_t *)q;
    if (c >= 'A' && c <= 'Z') c += 0x20;
    *w = (char)c;
  }
  return w;
}

char *lj_s390x_text_copy_upper(char *w, const char *q, MSize len)
{
  int mode = s390x_text_resolve_mode("LUAJIT_S390X_TEXT_TRANSFORM_MODE",
				     S390X_TEXT_GENERIC,
				     &s390x_text_transform_mode);
  if (mode == S390X_TEXT_ASCII8)
    return s390x_text_copy_upper_ascii8(w, q, len);
  char *e = w + len;
  for (; w < e; w++, q++) {
    uint32_t c = *(const uint8_t *)q;
    if (c >= 'a' && c <= 'z') c -= 0x20;
    *w = (char)c;
  }
  return w;
}

#endif
