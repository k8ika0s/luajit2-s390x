/*
** Experimental s390x decimal helpers.
** Current MVP uses exact software canonicalization plus packed/zoned
** conversions while keeping the public API opt-in.
*/

#include "lj_s390x_decimal.h"

#include <string.h>

#include "lj_arch.h"

static void decimal_normalize(char *digits, size_t *ndigits, size_t *scale,
			      int *negative)
{
  size_t lead = 0;
  size_t tail;

  while (lead < *ndigits && digits[lead] == '0')
    lead++;
  if (lead == *ndigits) {
    digits[0] = '0';
    digits[1] = '\0';
    *ndigits = 1;
    *scale = 0;
    *negative = 0;
    return;
  }
  if (lead != 0) {
    memmove(digits, digits + lead, *ndigits - lead);
    *ndigits -= lead;
  }

  tail = *ndigits;
  while (*scale > 0 && tail > 1 && digits[tail-1] == '0') {
    tail--;
    (*scale)--;
  }
  *ndigits = tail;
  digits[*ndigits] = '\0';
}

static int decimal_sign_nibble(int nibble, int *negative)
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

void lj_s390x_decimal_get_caps(S390XDecimalCaps *caps)
{
  memset(caps, 0, sizeof(*caps));
  caps->available = 1;
  caps->software = 1;
  caps->packed_decimal = 1;
  caps->zoned_decimal = 1;
#if LJ_TARGET_S390X
  caps->arch_s390x = 1;
#endif
}

const char *lj_s390x_decimal_error(int err)
{
  switch (err) {
  case LJ_S390X_DECIMAL_OK:
    return "ok";
  case LJ_S390X_DECIMAL_ERR_EMPTY:
    return "empty decimal input";
  case LJ_S390X_DECIMAL_ERR_FORMAT:
    return "invalid decimal format";
  case LJ_S390X_DECIMAL_ERR_DIGIT:
    return "invalid decimal digit";
  case LJ_S390X_DECIMAL_ERR_SIGN:
    return "invalid decimal sign";
  case LJ_S390X_DECIMAL_ERR_SCALE:
    return "invalid decimal scale";
  case LJ_S390X_DECIMAL_ERR_PACKED:
    return "invalid packed decimal input";
  case LJ_S390X_DECIMAL_ERR_ZONED:
    return "invalid zoned decimal input";
  default:
    return "unknown decimal error";
  }
}

int lj_s390x_decimal_parse(const char *src, size_t len, char *digits,
			   size_t *ndigits, size_t *scale, int *negative)
{
  size_t i = 0;
  size_t out = 0;
  int seen_point = 0;
  int seen_digit = 0;

  *negative = 0;
  *scale = 0;

  if (len == 0)
    return LJ_S390X_DECIMAL_ERR_EMPTY;
  if (src[0] == '+' || src[0] == '-') {
    *negative = (src[0] == '-');
    i = 1;
  }
  if (i == len)
    return LJ_S390X_DECIMAL_ERR_EMPTY;

  for (; i < len; i++) {
    char c = src[i];
    if (c >= '0' && c <= '9') {
      digits[out++] = c;
      if (seen_point)
	(*scale)++;
      seen_digit = 1;
    } else if (c == '.') {
      if (seen_point)
	return LJ_S390X_DECIMAL_ERR_FORMAT;
      seen_point = 1;
    } else {
      return LJ_S390X_DECIMAL_ERR_FORMAT;
    }
  }

  if (!seen_digit)
    return LJ_S390X_DECIMAL_ERR_EMPTY;

  *ndigits = out;
  digits[out] = '\0';
  decimal_normalize(digits, ndigits, scale, negative);
  return LJ_S390X_DECIMAL_OK;
}

int lj_s390x_decimal_from_packed(const uint8_t *bytes, size_t len,
				 size_t *scale, char *digits, size_t *ndigits,
				 int *negative)
{
  size_t i;
  size_t out = 0;
  int err;

  if (len == 0)
    return LJ_S390X_DECIMAL_ERR_PACKED;

  for (i = 0; i < len; i++) {
    int high = (bytes[i] >> 4) & 0x0f;
    int low = bytes[i] & 0x0f;
    if (i + 1 == len) {
      if (high > 9)
	return LJ_S390X_DECIMAL_ERR_PACKED;
      digits[out++] = (char)('0' + high);
      err = decimal_sign_nibble(low, negative);
      if (err != LJ_S390X_DECIMAL_OK)
	return err;
    } else {
      if (high > 9 || low > 9)
	return LJ_S390X_DECIMAL_ERR_PACKED;
      digits[out++] = (char)('0' + high);
      digits[out++] = (char)('0' + low);
    }
  }

  *ndigits = out;
  digits[out] = '\0';
  decimal_normalize(digits, ndigits, scale, negative);
  return LJ_S390X_DECIMAL_OK;
}

int lj_s390x_decimal_to_packed(const S390XDecimalView *dec, size_t digits,
			       uint8_t *bytes, size_t *len)
{
  size_t bytes_len;
  size_t total_nibbles;
  size_t digit_nibbles;
  size_t start;
  size_t i;

  if (digits == 0)
    digits = dec->ndigits;
  if (digits < dec->ndigits)
    return LJ_S390X_DECIMAL_ERR_SCALE;

  bytes_len = (digits + 2) / 2;
  total_nibbles = bytes_len * 2;
  digit_nibbles = total_nibbles - 1;
  start = digit_nibbles - digits;

  memset(bytes, 0, bytes_len);
  for (i = 0; i < digits; i++) {
    size_t nibble = start + i;
    uint8_t digit = (uint8_t)(i < digits - dec->ndigits ?
			      0 : (dec->digits[i - (digits - dec->ndigits)] - '0'));
    size_t byte = nibble / 2;
    if ((nibble & 1) == 0)
      bytes[byte] |= (uint8_t)(digit << 4);
    else
      bytes[byte] |= digit;
  }
  bytes[bytes_len-1] |= (uint8_t)(dec->negative ? 0x0d : 0x0c);
  *len = bytes_len;
  return LJ_S390X_DECIMAL_OK;
}

int lj_s390x_decimal_from_zoned(const uint8_t *bytes, size_t len,
				size_t *scale, char *digits, size_t *ndigits,
				int *negative)
{
  size_t i;

  if (len == 0)
    return LJ_S390X_DECIMAL_ERR_ZONED;

  for (i = 0; i < len; i++) {
    int zone = (bytes[i] >> 4) & 0x0f;
    int digit = bytes[i] & 0x0f;
    if (digit > 9)
      return LJ_S390X_DECIMAL_ERR_ZONED;
    if (i + 1 == len) {
      if (zone == 0x0d)
	*negative = 1;
      else if (zone == 0x0c || zone == 0x0f)
	*negative = 0;
      else
	return LJ_S390X_DECIMAL_ERR_SIGN;
    } else if (zone != 0x0f) {
      return LJ_S390X_DECIMAL_ERR_ZONED;
    }
    digits[i] = (char)('0' + digit);
  }

  *ndigits = len;
  digits[len] = '\0';
  decimal_normalize(digits, ndigits, scale, negative);
  return LJ_S390X_DECIMAL_OK;
}

int lj_s390x_decimal_to_zoned(const S390XDecimalView *dec, size_t digits,
			      uint8_t *bytes, size_t *len)
{
  size_t pad;
  size_t i;

  if (digits == 0)
    digits = dec->ndigits;
  if (digits < dec->ndigits)
    return LJ_S390X_DECIMAL_ERR_SCALE;

  pad = digits - dec->ndigits;
  for (i = 0; i < digits; i++) {
    uint8_t digit = (uint8_t)(i < pad ? 0 : (dec->digits[i - pad] - '0'));
    bytes[i] = (uint8_t)(0xf0 | digit);
  }
  bytes[digits-1] = (uint8_t)((dec->negative ? 0xd0 : 0xc0) |
			      (bytes[digits-1] & 0x0f));
  *len = digits;
  return LJ_S390X_DECIMAL_OK;
}

static int decimal_abs_compare(const S390XDecimalView *lhs,
			       const S390XDecimalView *rhs)
{
  ptrdiff_t lhs_exp = (ptrdiff_t)lhs->ndigits - (ptrdiff_t)lhs->scale;
  ptrdiff_t rhs_exp = (ptrdiff_t)rhs->ndigits - (ptrdiff_t)rhs->scale;
  size_t i;
  size_t maxdigits;

  if (lhs_exp != rhs_exp)
    return lhs_exp < rhs_exp ? -1 : 1;

  maxdigits = lhs->ndigits > rhs->ndigits ? lhs->ndigits : rhs->ndigits;
  for (i = 0; i < maxdigits; i++) {
    char ld = i < lhs->ndigits ? lhs->digits[i] : '0';
    char rd = i < rhs->ndigits ? rhs->digits[i] : '0';
    if (ld != rd)
      return ld < rd ? -1 : 1;
  }
  return 0;
}

static size_t decimal_effective_digits(const S390XDecimalView *dec,
				       size_t common_scale)
{
  return dec->ndigits + (common_scale - dec->scale);
}

static uint8_t decimal_digit_from_right(const S390XDecimalView *dec,
					size_t common_scale, size_t pos)
{
  size_t pad = common_scale - dec->scale;

  if (pos < pad)
    return 0;
  pos -= pad;
  if (pos >= dec->ndigits)
    return 0;
  return (uint8_t)(dec->digits[dec->ndigits - 1 - pos] - '0');
}

static void decimal_set_digit_from_right(char *digits, size_t total, size_t pos,
					 uint8_t digit)
{
  digits[total - 1 - pos] = (char)('0' + digit);
}

static void decimal_zero_result(char *digits, size_t *ndigits, size_t *scale,
				int *negative)
{
  digits[0] = '0';
  digits[1] = '\0';
  *ndigits = 1;
  *scale = 0;
  *negative = 0;
}

static int decimal_abs_add(const S390XDecimalView *lhs,
			   const S390XDecimalView *rhs,
			   char *digits, size_t *ndigits, size_t *scale,
			   int *negative)
{
  size_t common_scale = lhs->scale > rhs->scale ? lhs->scale : rhs->scale;
  size_t lhs_digits = decimal_effective_digits(lhs, common_scale);
  size_t rhs_digits = decimal_effective_digits(rhs, common_scale);
  size_t total = (lhs_digits > rhs_digits ? lhs_digits : rhs_digits) + 1;
  size_t pos;
  int carry = 0;

  for (pos = 0; pos < total; pos++) {
    int sum = carry +
	      decimal_digit_from_right(lhs, common_scale, pos) +
	      decimal_digit_from_right(rhs, common_scale, pos);
    decimal_set_digit_from_right(digits, total, pos, (uint8_t)(sum % 10));
    carry = sum / 10;
  }

  digits[total] = '\0';
  *ndigits = total;
  *scale = common_scale;
  decimal_normalize(digits, ndigits, scale, negative);
  return LJ_S390X_DECIMAL_OK;
}

static int decimal_abs_sub(const S390XDecimalView *lhs,
			   const S390XDecimalView *rhs,
			   char *digits, size_t *ndigits, size_t *scale,
			   int *negative)
{
  size_t common_scale = lhs->scale > rhs->scale ? lhs->scale : rhs->scale;
  size_t lhs_digits = decimal_effective_digits(lhs, common_scale);
  size_t rhs_digits = decimal_effective_digits(rhs, common_scale);
  size_t total = lhs_digits > rhs_digits ? lhs_digits : rhs_digits;
  size_t pos;
  int borrow = 0;

  for (pos = 0; pos < total; pos++) {
    int diff = (int)decimal_digit_from_right(lhs, common_scale, pos) -
	       (int)decimal_digit_from_right(rhs, common_scale, pos) -
	       borrow;
    if (diff < 0) {
      diff += 10;
      borrow = 1;
    } else {
      borrow = 0;
    }
    decimal_set_digit_from_right(digits, total, pos, (uint8_t)diff);
  }

  digits[total] = '\0';
  *ndigits = total;
  *scale = common_scale;
  decimal_normalize(digits, ndigits, scale, negative);
  return LJ_S390X_DECIMAL_OK;
}

int lj_s390x_decimal_compare(const S390XDecimalView *lhs,
			     const S390XDecimalView *rhs)
{
  int cmp;

  if (lhs->negative != rhs->negative) {
    if (lhs->ndigits == 1 && lhs->digits[0] == '0' &&
	rhs->ndigits == 1 && rhs->digits[0] == '0')
      return 0;
    return lhs->negative ? -1 : 1;
  }

  cmp = decimal_abs_compare(lhs, rhs);
  return lhs->negative ? -cmp : cmp;
}

int lj_s390x_decimal_add(const S390XDecimalView *lhs,
			 const S390XDecimalView *rhs,
			 char *digits, size_t *ndigits, size_t *scale,
			 int *negative)
{
  int cmp;

  if (lhs->negative == rhs->negative) {
    *negative = lhs->negative;
    return decimal_abs_add(lhs, rhs, digits, ndigits, scale, negative);
  }

  cmp = decimal_abs_compare(lhs, rhs);
  if (cmp == 0) {
    decimal_zero_result(digits, ndigits, scale, negative);
    return LJ_S390X_DECIMAL_OK;
  }
  if (cmp > 0) {
    *negative = lhs->negative;
    return decimal_abs_sub(lhs, rhs, digits, ndigits, scale, negative);
  }
  *negative = rhs->negative;
  return decimal_abs_sub(rhs, lhs, digits, ndigits, scale, negative);
}

int lj_s390x_decimal_sub(const S390XDecimalView *lhs,
			 const S390XDecimalView *rhs,
			 char *digits, size_t *ndigits, size_t *scale,
			 int *negative)
{
  S390XDecimalView neg_rhs = *rhs;
  neg_rhs.negative = (uint8_t)!rhs->negative;
  return lj_s390x_decimal_add(lhs, &neg_rhs, digits, ndigits, scale, negative);
}
