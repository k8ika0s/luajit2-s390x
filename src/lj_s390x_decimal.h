#ifndef _LJ_S390X_DECIMAL_H
#define _LJ_S390X_DECIMAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t negative;
  size_t scale;
  size_t ndigits;
  const char *digits;
} S390XDecimalView;

typedef struct {
  uint8_t available;
  uint8_t arch_s390x;
  uint8_t software;
  uint8_t hardware_dfp;
  uint8_t packed_decimal;
  uint8_t zoned_decimal;
} S390XDecimalCaps;

enum {
  LJ_S390X_DECIMAL_OK = 0,
  LJ_S390X_DECIMAL_ERR_EMPTY,
  LJ_S390X_DECIMAL_ERR_FORMAT,
  LJ_S390X_DECIMAL_ERR_DIGIT,
  LJ_S390X_DECIMAL_ERR_SIGN,
  LJ_S390X_DECIMAL_ERR_SCALE,
  LJ_S390X_DECIMAL_ERR_PACKED,
  LJ_S390X_DECIMAL_ERR_ZONED
};

void lj_s390x_decimal_get_caps(S390XDecimalCaps *caps);
const char *lj_s390x_decimal_error(int err);
int lj_s390x_decimal_parse(const char *src, size_t len, char *digits,
			   size_t *ndigits, size_t *scale, int *negative);
int lj_s390x_decimal_from_packed(const uint8_t *bytes, size_t len,
				 size_t *scale, char *digits, size_t *ndigits,
				 int *negative);
int lj_s390x_decimal_to_packed(const S390XDecimalView *dec, size_t digits,
			       uint8_t *bytes, size_t *len);
int lj_s390x_decimal_from_zoned(const uint8_t *bytes, size_t len,
				size_t *scale, char *digits, size_t *ndigits,
				int *negative);
int lj_s390x_decimal_to_zoned(const S390XDecimalView *dec, size_t digits,
			      uint8_t *bytes, size_t *len);
int lj_s390x_decimal_compare(const S390XDecimalView *lhs,
			     const S390XDecimalView *rhs);
int lj_s390x_decimal_add(const S390XDecimalView *lhs,
			 const S390XDecimalView *rhs,
			 char *digits, size_t *ndigits, size_t *scale,
			 int *negative);
int lj_s390x_decimal_sub(const S390XDecimalView *lhs,
			 const S390XDecimalView *rhs,
			 char *digits, size_t *ndigits, size_t *scale,
			 int *negative);

#endif
