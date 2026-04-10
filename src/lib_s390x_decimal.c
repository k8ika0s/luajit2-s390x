/*
** Opt-in experimental s390x decimal module.
*/

#define lib_s390x_decimal_c
#define LUA_LIB

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_s390x_decimal.h"

#define S390X_DECIMAL_MT "s390x.experimental.decimal"

typedef struct {
  uint8_t negative;
  uint32_t scale;
  uint32_t ndigits;
  char digits[1];
} S390XDecimalUD;

static S390XDecimalUD *decimal_check(lua_State *L, int idx)
{
  return (S390XDecimalUD *)luaL_checkudata(L, idx, S390X_DECIMAL_MT);
}

static S390XDecimalView decimal_view(const S390XDecimalUD *ud)
{
  S390XDecimalView view;
  view.negative = ud->negative;
  view.scale = ud->scale;
  view.ndigits = ud->ndigits;
  view.digits = ud->digits;
  return view;
}

static S390XDecimalUD *decimal_newud(lua_State *L, size_t maxdigits)
{
  size_t size = offsetof(S390XDecimalUD, digits) + maxdigits + 1;
  S390XDecimalUD *ud = (S390XDecimalUD *)lua_newuserdata(L, size);
  memset(ud, 0, size);
  luaL_getmetatable(L, S390X_DECIMAL_MT);
  lua_setmetatable(L, -2);
  return ud;
}

static void decimal_set_ud(S390XDecimalUD *ud, const char *digits, size_t ndigits,
			   size_t scale, int negative)
{
  if (digits != ud->digits)
    memcpy(ud->digits, digits, ndigits + 1);
  ud->negative = (uint8_t)negative;
  ud->scale = (uint32_t)scale;
  ud->ndigits = (uint32_t)ndigits;
}

static int decimal_push_view_string(lua_State *L, const S390XDecimalView *view)
{
  ptrdiff_t exp = (ptrdiff_t)view->ndigits - (ptrdiff_t)view->scale;
  size_t outlen;
  char stackbuf[128];
  char *out = stackbuf;
  char *p;
  int heap = 0;

  if (view->scale == 0)
    outlen = view->ndigits + (view->negative ? 1 : 0);
  else if (exp > 0)
    outlen = view->ndigits + 1 + (view->negative ? 1 : 0);
  else
    outlen = view->ndigits + (size_t)(-exp) + 2 + (view->negative ? 1 : 0);

  if (outlen > sizeof(stackbuf)) {
    out = (char *)malloc(outlen);
    if (out == NULL)
      return luaL_error(L, "decimal string buffer alloc failed");
    heap = 1;
  }

  p = out;
  if (view->negative)
    *p++ = '-';
  if (view->scale == 0) {
    memcpy(p, view->digits, view->ndigits);
  } else if (exp > 0) {
    memcpy(p, view->digits, (size_t)exp);
    p += (size_t)exp;
    *p++ = '.';
    memcpy(p, view->digits + exp, view->ndigits - (size_t)exp);
  } else {
    size_t zeros = (size_t)(-exp);
    *p++ = '0';
    *p++ = '.';
    memset(p, '0', zeros);
    p += zeros;
    memcpy(p, view->digits, view->ndigits);
  }

  lua_pushlstring(L, out, outlen);
  if (heap)
    free(out);
  return 1;
}

static int decimal_push_string(lua_State *L, const S390XDecimalUD *ud)
{
  S390XDecimalView view = decimal_view(ud);
  return decimal_push_view_string(L, &view);
}

static int decimal_set_from_string(lua_State *L, const char *src, size_t len)
{
  S390XDecimalUD *ud = decimal_newud(L, len);
  size_t ndigits = 0;
  size_t scale = 0;
  int negative = 0;
  int err = lj_s390x_decimal_parse(src, len, ud->digits, &ndigits, &scale,
				   &negative);
  if (err != LJ_S390X_DECIMAL_OK)
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  decimal_set_ud(ud, ud->digits, ndigits, scale, negative);
  return 1;
}

static int decimal_set_from_packed(lua_State *L, const uint8_t *src, size_t len,
				   size_t scale)
{
  S390XDecimalUD *ud = decimal_newud(L, len * 2);
  size_t ndigits = 0;
  size_t outscale = scale;
  int negative = 0;
  int err = lj_s390x_decimal_from_packed(src, len, &outscale, ud->digits,
					 &ndigits, &negative);
  if (err != LJ_S390X_DECIMAL_OK)
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  decimal_set_ud(ud, ud->digits, ndigits, outscale, negative);
  return 1;
}

static int decimal_set_from_zoned(lua_State *L, const uint8_t *src, size_t len,
				  size_t scale)
{
  S390XDecimalUD *ud = decimal_newud(L, len);
  size_t ndigits = 0;
  size_t outscale = scale;
  int negative = 0;
  int err = lj_s390x_decimal_from_zoned(src, len, &outscale, ud->digits,
					&ndigits, &negative);
  if (err != LJ_S390X_DECIMAL_OK)
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  decimal_set_ud(ud, ud->digits, ndigits, outscale, negative);
  return 1;
}

static int decimal_binary_op(lua_State *L, int subtract)
{
  S390XDecimalUD *lhs = decimal_check(L, 1);
  S390XDecimalUD *rhs = decimal_check(L, 2);
  S390XDecimalView lhs_view = decimal_view(lhs);
  S390XDecimalView rhs_view = decimal_view(rhs);
  size_t maxdigits = lhs->ndigits + rhs->ndigits + 2;
  S390XDecimalUD *out = decimal_newud(L, maxdigits);
  size_t ndigits = 0;
  size_t scale = 0;
  int negative = 0;
  int err = subtract ?
    lj_s390x_decimal_sub(&lhs_view, &rhs_view, out->digits, &ndigits, &scale,
			 &negative) :
    lj_s390x_decimal_add(&lhs_view, &rhs_view, out->digits, &ndigits, &scale,
			 &negative);
  if (err != LJ_S390X_DECIMAL_OK)
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  decimal_set_ud(out, out->digits, ndigits, scale, negative);
  return 1;
}

static int decimal_check_digits_arg(lua_State *L, int idx)
{
  lua_Integer n = luaL_optinteger(L, idx, 0);
  if (n < 0)
    luaL_error(L, "digits must be non-negative");
  return (int)n;
}

static int decimal_check_scale_arg(lua_State *L, int idx)
{
  lua_Integer n = luaL_optinteger(L, idx, 0);
  if (n < 0)
    luaL_error(L, "scale must be non-negative");
  return (int)n;
}

static int decimal_capabilities(lua_State *L)
{
  S390XDecimalCaps caps;
  lj_s390x_decimal_get_caps(&caps);
  lua_createtable(L, 0, 7);
  lua_pushboolean(L, caps.available);
  lua_setfield(L, -2, "available");
  lua_pushboolean(L, caps.arch_s390x);
  lua_setfield(L, -2, "arch_s390x");
  lua_pushstring(L, caps.arch_s390x ? "s390x" : "generic");
  lua_setfield(L, -2, "arch");
  lua_pushboolean(L, caps.software);
  lua_setfield(L, -2, "software");
  lua_pushstring(L, caps.software ? "software" : "disabled");
  lua_setfield(L, -2, "backend");
  lua_pushboolean(L, caps.hardware_dfp);
  lua_setfield(L, -2, "hardware_dfp");
  lua_pushboolean(L, caps.packed_decimal);
  lua_setfield(L, -2, "packed_decimal");
  lua_pushboolean(L, caps.zoned_decimal);
  lua_setfield(L, -2, "zoned_decimal");
  return 1;
}

static int decimal_new(lua_State *L)
{
  size_t len = 0;
  const char *src = luaL_checklstring(L, 1, &len);
  if (!lua_isnoneornil(L, 2)) {
    const char *format = luaL_checkstring(L, 2);
    if (strcmp(format, "string") != 0)
      return luaL_error(L, "unsupported decimal format '%s'", format);
  }
  return decimal_set_from_string(L, src, len);
}

static int decimal_tostring(lua_State *L)
{
  S390XDecimalUD *ud = decimal_check(L, 1);
  return decimal_push_string(L, ud);
}

static int decimal_cmp(lua_State *L)
{
  S390XDecimalUD *lhs = decimal_check(L, 1);
  S390XDecimalUD *rhs = decimal_check(L, 2);
  S390XDecimalView lhs_view = decimal_view(lhs);
  S390XDecimalView rhs_view = decimal_view(rhs);
  lua_pushinteger(L, lj_s390x_decimal_compare(&lhs_view, &rhs_view));
  return 1;
}

static int decimal_add(lua_State *L)
{
  return decimal_binary_op(L, 0);
}

static int decimal_sub(lua_State *L)
{
  return decimal_binary_op(L, 1);
}

static int decimal_from_packed(lua_State *L)
{
  size_t len = 0;
  const uint8_t *src = (const uint8_t *)luaL_checklstring(L, 1, &len);
  int scale = decimal_check_scale_arg(L, 2);
  return decimal_set_from_packed(L, src, len, (size_t)scale);
}

static int decimal_packed_digit_at(const uint8_t *src, size_t idx);
static void decimal_packed_set_digit(uint8_t *dst, size_t idx, int digit);
static int decimal_packed_sign_nibble(int nibble, int *negative);

static int decimal_to_packed(lua_State *L)
{
  S390XDecimalUD *ud = decimal_check(L, 1);
  S390XDecimalView view = decimal_view(ud);
  int digits = decimal_check_digits_arg(L, 2);
  size_t outlen = 0;
  size_t capacity = (size_t)(((digits > 0 ? digits : (int)view.ndigits) + 2) / 2);
  uint8_t stackbuf[64];
  uint8_t *out = stackbuf;
  int heap = 0;
  int err;
  if (capacity > sizeof(stackbuf)) {
    out = (uint8_t *)malloc(capacity);
    if (out == NULL)
      return luaL_error(L, "decimal packed buffer alloc failed");
    heap = 1;
  }
  err = lj_s390x_decimal_to_packed(&view, (size_t)digits, out, &outlen);
  if (err != LJ_S390X_DECIMAL_OK)
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  lua_pushlstring(L, (const char *)out, outlen);
  if (heap)
    free(out);
  return 1;
}

static void decimal_packed_copy_digits(char *dst, const uint8_t *src,
				       size_t start, size_t count)
{
  size_t i;
  for (i = 0; i < count; i++)
    dst[i] = (char)('0' + decimal_packed_digit_at(src, start + i));
}

static int decimal_packed_to_string_fast(lua_State *L, const uint8_t *src,
					 size_t len, size_t scale)
{
  size_t source_digits = len * 2 - 1;
  size_t lead = 0;
  size_t tail = source_digits;
  size_t ndigits;
  ptrdiff_t exp;
  size_t outlen;
  char stackbuf[128];
  char *out = stackbuf;
  char *p;
  int negative = 0;
  int err;
  int heap = 0;
  size_t i;

  if (len == 0)
    return luaL_error(L, "%s", lj_s390x_decimal_error(LJ_S390X_DECIMAL_ERR_PACKED));

  err = decimal_packed_sign_nibble(src[len-1] & 0x0f, &negative);
  if (err != LJ_S390X_DECIMAL_OK)
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));

  for (i = 0; i < source_digits; i++) {
    if (decimal_packed_digit_at(src, i) > 9)
      return luaL_error(L, "%s",
			lj_s390x_decimal_error(LJ_S390X_DECIMAL_ERR_PACKED));
  }

  while (lead < source_digits && decimal_packed_digit_at(src, lead) == 0)
    lead++;
  if (lead == source_digits) {
    lead = 0;
    tail = 1;
    scale = 0;
    negative = 0;
  } else {
    while (scale > 0 && tail > lead + 1 &&
	   decimal_packed_digit_at(src, tail - 1) == 0) {
      tail--;
      scale--;
    }
  }

  ndigits = tail - lead;
  exp = (ptrdiff_t)ndigits - (ptrdiff_t)scale;
  if (scale == 0)
    outlen = ndigits + (negative ? 1 : 0);
  else if (exp > 0)
    outlen = ndigits + 1 + (negative ? 1 : 0);
  else
    outlen = ndigits + (size_t)(-exp) + 2 + (negative ? 1 : 0);

  if (outlen > sizeof(stackbuf)) {
    out = (char *)malloc(outlen);
    if (out == NULL)
      return luaL_error(L, "decimal packed string alloc failed");
    heap = 1;
  }

  p = out;
  if (negative)
    *p++ = '-';
  if (scale == 0) {
    decimal_packed_copy_digits(p, src, lead, ndigits);
  } else if (exp > 0) {
    decimal_packed_copy_digits(p, src, lead, (size_t)exp);
    p += (size_t)exp;
    *p++ = '.';
    decimal_packed_copy_digits(p, src, lead + (size_t)exp,
			       ndigits - (size_t)exp);
  } else {
    size_t zeros = (size_t)(-exp);
    *p++ = '0';
    *p++ = '.';
    memset(p, '0', zeros);
    p += zeros;
    decimal_packed_copy_digits(p, src, lead, ndigits);
  }

  lua_pushlstring(L, out, outlen);
  if (heap)
    free(out);
  return 1;
}

static int decimal_packed_to_string(lua_State *L)
{
  size_t len = 0;
  const uint8_t *src = (const uint8_t *)luaL_checklstring(L, 1, &len);
  int scale = decimal_check_scale_arg(L, 2);
  return decimal_packed_to_string_fast(L, src, len, (size_t)scale);
}

static int decimal_string_to_packed(lua_State *L)
{
  size_t len = 0;
  const char *src = luaL_checklstring(L, 1, &len);
  int digits_arg = decimal_check_digits_arg(L, 2);
  char stack_digits[128];
  char *digits = stack_digits;
  size_t capacity = len + 1;
  size_t ndigits = 0;
  size_t scale = 0;
  int negative = 0;
  size_t outlen = 0;
  size_t outcap;
  uint8_t stack_out[64];
  uint8_t *out = stack_out;
  int digits_out;
  int digits_heap = 0;
  int out_heap = 0;
  int err;
  S390XDecimalView view;

  if (capacity > sizeof(stack_digits)) {
    digits = (char *)malloc(capacity);
    if (digits == NULL)
      return luaL_error(L, "decimal parse alloc failed");
    digits_heap = 1;
  }
  err = lj_s390x_decimal_parse(src, len, digits, &ndigits, &scale, &negative);
  if (err != LJ_S390X_DECIMAL_OK) {
    if (digits_heap)
      free(digits);
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  }

  digits_out = digits_arg > 0 ? digits_arg : (int)ndigits;
  outcap = (size_t)((digits_out + 2) / 2);
  if (outcap > sizeof(stack_out)) {
    out = (uint8_t *)malloc(outcap);
    if (out == NULL) {
      if (digits_heap)
	free(digits);
      return luaL_error(L, "decimal packed encode alloc failed");
    }
    out_heap = 1;
  }

  view.negative = (uint8_t)negative;
  view.scale = scale;
  view.ndigits = ndigits;
  view.digits = digits;
  err = lj_s390x_decimal_to_packed(&view, (size_t)digits_out, out, &outlen);
  if (err != LJ_S390X_DECIMAL_OK) {
    if (out_heap)
      free(out);
    if (digits_heap)
      free(digits);
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  }
  lua_pushlstring(L, (const char *)out, outlen);
  if (out_heap)
    free(out);
  if (digits_heap)
    free(digits);
  return 1;
}

static int decimal_packed_digit_at(const uint8_t *src, size_t idx)
{
  uint8_t byte = src[idx >> 1];
  if ((idx & 1) == 0)
    return (byte >> 4) & 0x0f;
  return byte & 0x0f;
}

static void decimal_packed_set_digit(uint8_t *dst, size_t idx, int digit)
{
  size_t byte = idx >> 1;
  if ((idx & 1) == 0)
    dst[byte] |= (uint8_t)(digit << 4);
  else
    dst[byte] |= (uint8_t)digit;
}

static int decimal_packed_sign_nibble(int nibble, int *negative)
{
  switch (nibble) {
  case 0x0a:
  case 0x0c:
  case 0x0e:
  case 0x0f:
    *negative = 0;
    return LJ_S390X_DECIMAL_OK;
  case 0x0b:
  case 0x0d:
    *negative = 1;
    return LJ_S390X_DECIMAL_OK;
  default:
    return LJ_S390X_DECIMAL_ERR_SIGN;
  }
}

static int decimal_packed_rescale_fast(lua_State *L, const uint8_t *src,
				       size_t len, size_t scale,
				       int digits_arg)
{
  size_t source_digits = len * 2 - 1;
  size_t lead = 0;
  size_t tail = source_digits;
  size_t ndigits;
  int negative = 0;
  int err;
  int digits_out;
  size_t outlen;
  size_t out_digit_nibbles;
  size_t start;
  size_t i;
  uint8_t stack_out[64];
  uint8_t *out = stack_out;
  int heap = 0;

  if (len == 0)
    return luaL_error(L, "%s", lj_s390x_decimal_error(LJ_S390X_DECIMAL_ERR_PACKED));

  err = decimal_packed_sign_nibble(src[len-1] & 0x0f, &negative);
  if (err != LJ_S390X_DECIMAL_OK)
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));

  for (i = 0; i < source_digits; i++) {
    if (decimal_packed_digit_at(src, i) > 9)
      return luaL_error(L, "%s",
			lj_s390x_decimal_error(LJ_S390X_DECIMAL_ERR_PACKED));
  }

  while (lead < source_digits && decimal_packed_digit_at(src, lead) == 0)
    lead++;
  if (lead == source_digits) {
    lead = 0;
    tail = 1;
    scale = 0;
    negative = 0;
  } else {
    while (scale > 0 && tail > lead + 1 &&
	   decimal_packed_digit_at(src, tail - 1) == 0) {
      tail--;
      scale--;
    }
  }

  ndigits = tail - lead;
  digits_out = digits_arg > 0 ? digits_arg : (int)ndigits;
  if ((size_t)digits_out < ndigits)
    return luaL_error(L, "%s", lj_s390x_decimal_error(LJ_S390X_DECIMAL_ERR_SCALE));

  outlen = (size_t)((digits_out + 2) / 2);
  if (outlen > sizeof(stack_out)) {
    out = (uint8_t *)malloc(outlen);
    if (out == NULL)
      return luaL_error(L, "decimal packed rescale output alloc failed");
    heap = 1;
  }

  memset(out, 0, outlen);
  out_digit_nibbles = outlen * 2 - 1;
  start = out_digit_nibbles - (size_t)digits_out;
  for (i = 0; i < (size_t)digits_out; i++) {
    int digit = 0;
    if (i >= (size_t)digits_out - ndigits)
      digit = decimal_packed_digit_at(src, lead + i - ((size_t)digits_out - ndigits));
    decimal_packed_set_digit(out, start + i, digit);
  }
  out[outlen-1] |= (uint8_t)(negative ? 0x0d : 0x0c);

  lua_pushlstring(L, (const char *)out, outlen);
  if (heap)
    free(out);
  return 1;
}

static int decimal_packed_rescale(lua_State *L)
{
  size_t len = 0;
  const uint8_t *src = (const uint8_t *)luaL_checklstring(L, 1, &len);
  int scale_arg = decimal_check_scale_arg(L, 2);
  int digits_arg = decimal_check_digits_arg(L, 3);
  return decimal_packed_rescale_fast(L, src, len, (size_t)scale_arg, digits_arg);
}

static int decimal_from_zoned(lua_State *L)
{
  size_t len = 0;
  const uint8_t *src = (const uint8_t *)luaL_checklstring(L, 1, &len);
  int scale = decimal_check_scale_arg(L, 2);
  return decimal_set_from_zoned(L, src, len, (size_t)scale);
}

static int decimal_to_zoned(lua_State *L)
{
  S390XDecimalUD *ud = decimal_check(L, 1);
  S390XDecimalView view = decimal_view(ud);
  int digits = decimal_check_digits_arg(L, 2);
  size_t outlen = 0;
  size_t capacity = (size_t)(digits > 0 ? digits : (int)view.ndigits);
  uint8_t stackbuf[64];
  uint8_t *out = stackbuf;
  int heap = 0;
  int err;
  if (capacity > sizeof(stackbuf)) {
    out = (uint8_t *)malloc(capacity);
    if (out == NULL)
      return luaL_error(L, "decimal zoned buffer alloc failed");
    heap = 1;
  }
  err = lj_s390x_decimal_to_zoned(&view, (size_t)digits, out, &outlen);
  if (err != LJ_S390X_DECIMAL_OK)
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  lua_pushlstring(L, (const char *)out, outlen);
  if (heap)
    free(out);
  return 1;
}

static int decimal_zoned_to_string(lua_State *L)
{
  size_t len = 0;
  const uint8_t *src = (const uint8_t *)luaL_checklstring(L, 1, &len);
  int scale = decimal_check_scale_arg(L, 2);
  size_t outscale = (size_t)scale;
  char stack_digits[128];
  char *digits = stack_digits;
  size_t capacity = len + 1;
  size_t ndigits = 0;
  int negative = 0;
  int heap = 0;
  int err;
  S390XDecimalView view;

  if (capacity > sizeof(stack_digits)) {
    digits = (char *)malloc(capacity);
    if (digits == NULL)
      return luaL_error(L, "decimal zoned decode alloc failed");
    heap = 1;
  }
  err = lj_s390x_decimal_from_zoned(src, len, &outscale, digits, &ndigits,
				    &negative);
  if (err != LJ_S390X_DECIMAL_OK) {
    if (heap)
      free(digits);
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  }
  view.negative = (uint8_t)negative;
  view.scale = outscale;
  view.ndigits = ndigits;
  view.digits = digits;
  decimal_push_view_string(L, &view);
  if (heap)
    free(digits);
  return 1;
}

static int decimal_string_to_zoned(lua_State *L)
{
  size_t len = 0;
  const char *src = luaL_checklstring(L, 1, &len);
  int digits_arg = decimal_check_digits_arg(L, 2);
  char stack_digits[128];
  char *digits = stack_digits;
  size_t capacity = len + 1;
  size_t ndigits = 0;
  size_t scale = 0;
  int negative = 0;
  size_t outlen = 0;
  size_t outcap;
  uint8_t stack_out[64];
  uint8_t *out = stack_out;
  int digits_out;
  int digits_heap = 0;
  int out_heap = 0;
  int err;
  S390XDecimalView view;

  if (capacity > sizeof(stack_digits)) {
    digits = (char *)malloc(capacity);
    if (digits == NULL)
      return luaL_error(L, "decimal parse alloc failed");
    digits_heap = 1;
  }
  err = lj_s390x_decimal_parse(src, len, digits, &ndigits, &scale, &negative);
  if (err != LJ_S390X_DECIMAL_OK) {
    if (digits_heap)
      free(digits);
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  }

  digits_out = digits_arg > 0 ? digits_arg : (int)ndigits;
  outcap = (size_t)digits_out;
  if (outcap > sizeof(stack_out)) {
    out = (uint8_t *)malloc(outcap);
    if (out == NULL) {
      if (digits_heap)
	free(digits);
      return luaL_error(L, "decimal zoned encode alloc failed");
    }
    out_heap = 1;
  }

  view.negative = (uint8_t)negative;
  view.scale = scale;
  view.ndigits = ndigits;
  view.digits = digits;
  err = lj_s390x_decimal_to_zoned(&view, (size_t)digits_out, out, &outlen);
  if (err != LJ_S390X_DECIMAL_OK) {
    if (out_heap)
      free(out);
    if (digits_heap)
      free(digits);
    return luaL_error(L, "%s", lj_s390x_decimal_error(err));
  }
  lua_pushlstring(L, (const char *)out, outlen);
  if (out_heap)
    free(out);
  if (digits_heap)
    free(digits);
  return 1;
}

static int decimal_meta_tostring(lua_State *L)
{
  return decimal_tostring(L);
}

static int decimal_meta_eq(lua_State *L)
{
  S390XDecimalUD *lhs = decimal_check(L, 1);
  S390XDecimalUD *rhs = decimal_check(L, 2);
  S390XDecimalView lhs_view = decimal_view(lhs);
  S390XDecimalView rhs_view = decimal_view(rhs);
  lua_pushboolean(L, lj_s390x_decimal_compare(&lhs_view, &rhs_view) == 0);
  return 1;
}

LUALIB_API int luaopen_s390x_experimental_decimal(lua_State *L)
{
  static const luaL_Reg decimal_lib[] = {
    { "capabilities", decimal_capabilities },
    { "new", decimal_new },
    { "tostring", decimal_tostring },
    { "add", decimal_add },
    { "sub", decimal_sub },
    { "cmp", decimal_cmp },
    { "from_packed", decimal_from_packed },
    { "to_packed", decimal_to_packed },
    { "packed_to_string", decimal_packed_to_string },
    { "string_to_packed", decimal_string_to_packed },
    { "packed_rescale", decimal_packed_rescale },
    { "from_zoned", decimal_from_zoned },
    { "to_zoned", decimal_to_zoned },
    { "zoned_to_string", decimal_zoned_to_string },
    { "string_to_zoned", decimal_string_to_zoned },
    { NULL, NULL }
  };
  static const luaL_Reg decimal_meta[] = {
    { "__tostring", decimal_meta_tostring },
    { "__eq", decimal_meta_eq },
    { NULL, NULL }
  };

  if (luaL_newmetatable(L, S390X_DECIMAL_MT))
    luaL_register(L, NULL, decimal_meta);
  lua_pop(L, 1);
  luaL_register(L, "s390x.experimental.decimal", decimal_lib);
  return 1;
}
