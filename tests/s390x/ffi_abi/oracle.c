#include <stdint.h>
#include <stdarg.h>
#include <complex.h>
#include <string.h>

struct small_u8 {
  uint8_t a;
};

struct small_u16 {
  uint16_t a;
};

struct small_u32 {
  uint32_t a;
};

struct small_u64 {
  uint32_t a;
  uint32_t b;
};

struct one_float {
  float a;
};

struct one_double {
  double a;
};

struct big_pair {
  uint64_t a;
  uint64_t b;
};

struct hfa2d {
  double a;
  double b;
};

enum probe_color {
  PROBE_RED = 11,
  PROBE_GREEN = 13,
  PROBE_BLUE = 17
};

typedef int32_t (*i32_callback)(int32_t);

int8_t echo_i8(int8_t value) { return value; }
uint8_t echo_u8(uint8_t value) { return value; }
int16_t echo_i16(int16_t value) { return value; }
uint16_t echo_u16(uint16_t value) { return value; }
int32_t echo_i32(int32_t value) { return value; }
uint32_t echo_u32(uint32_t value) { return value; }
int64_t echo_i64(int64_t value) { return value; }
uint64_t echo_u64(uint64_t value) { return value; }

float add_float(float a, float b) { return a + b; }
double add_double(double a, double b) { return a + b; }
float complex add_complexf(float complex a, float complex b) { return a + b; }
double complex add_complex(double complex a, double complex b) { return a + b; }
double complex mul_complex(double complex a, double complex b) { return a * b; }
float take_complexf_sum(float complex value)
{
  return crealf(value) + 10.0f * cimagf(value);
}
double take_complex_sum(double complex value)
{
  return creal(value) + 10.0 * cimag(value);
}

float take_complexf_pair(float seed, float complex a, float complex b)
{
  return seed + crealf(a) + 3.0f * cimagf(a) +
	 5.0f * crealf(b) + 7.0f * cimagf(b);
}

double take_complex_pair(double seed, double complex a, double complex b)
{
  return seed + creal(a) + 3.0 * cimag(a) +
	 5.0 * creal(b) + 7.0 * cimag(b);
}

float mutate_complexf_arg(float complex value)
{
  volatile float complex *p = &value;
  *p = 31.0f + 7.0f * I;
  return crealf(*p) + cimagf(*p);
}

double mutate_complex_arg(double complex value)
{
  volatile double complex *p = &value;
  *p = 31.0 + 7.0 * I;
  return creal(*p) + cimag(*p);
}

struct small_u8 echo_small_u8(struct small_u8 value) { return value; }
struct small_u16 echo_small_u16(struct small_u16 value) { return value; }
struct small_u32 echo_small_u32(struct small_u32 value) { return value; }
struct small_u64 echo_small_u64(struct small_u64 value) { return value; }
struct big_pair echo_big_pair(struct big_pair value) { return value; }
struct hfa2d echo_hfa2d(struct hfa2d value) { return value; }

uint64_t take_small_u8(struct small_u8 value) { return value.a; }
uint64_t take_small_u16(struct small_u16 value) { return value.a; }
uint64_t take_small_u32(struct small_u32 value) { return value.a; }
uint64_t take_small_u64(struct small_u64 value) { return value.a + value.b; }
double take_one_float(struct one_float value) { return value.a; }
double take_one_double(struct one_double value) { return value.a; }
uint64_t take_big_pair(struct big_pair value) { return value.a + value.b; }
double take_hfa2d(struct hfa2d value) { return value.a + value.b; }

uint64_t take6_small_u32(struct small_u32 a, struct small_u32 b,
			 struct small_u32 c, struct small_u32 d,
			 struct small_u32 e, struct small_u32 f)
{
  return (uint64_t)a.a + b.a + c.a + d.a + e.a + f.a;
}

uint64_t take7_small_u32(struct small_u32 a, struct small_u32 b,
			 struct small_u32 c, struct small_u32 d,
			 struct small_u32 e, struct small_u32 f,
			 struct small_u32 g)
{
  return take6_small_u32(a, b, c, d, e, f) + g.a;
}

uint64_t take6_small_u64(struct small_u64 a, struct small_u64 b,
			 struct small_u64 c, struct small_u64 d,
			 struct small_u64 e, struct small_u64 f)
{
  return (uint64_t)a.a + a.b + b.a + b.b + c.a + c.b +
	 d.a + d.b + e.a + e.b + f.a + f.b;
}

uint64_t take7_small_u64(struct small_u64 a, struct small_u64 b,
			 struct small_u64 c, struct small_u64 d,
			 struct small_u64 e, struct small_u64 f,
			 struct small_u64 g)
{
  return take6_small_u64(a, b, c, d, e, f) + g.a + g.b;
}

double take6_one_double(struct one_double a, struct one_double b,
			struct one_double c, struct one_double d,
			struct one_double e, struct one_double f)
{
  return a.a + b.a + c.a + d.a + e.a + f.a;
}

double take7_one_double(struct one_double a, struct one_double b,
			struct one_double c, struct one_double d,
			struct one_double e, struct one_double f,
			struct one_double g)
{
  return take6_one_double(a, b, c, d, e, f) + g.a;
}

uint64_t sum_varargs(uint64_t seed, int count, ...)
{
  uint64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    total += va_arg(ap, uint64_t);
  }
  va_end(ap);
  return total;
}

double sum_varargs_double(double seed, int count, ...)
{
  double total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    total += va_arg(ap, double);
  }
  va_end(ap);
  return total;
}

double sum_varargs_mixed(uint64_t seed, int pairs, ...)
{
  double total = (double)seed;
  va_list ap;
  int i;
  va_start(ap, pairs);
  for (i = 0; i < pairs; i++) {
    total += (double)va_arg(ap, uint64_t);
    total += va_arg(ap, double);
  }
  va_end(ap);
  return total;
}

int64_t sum_varargs_i32(int32_t seed, int count, ...)
{
  int64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    total += va_arg(ap, int32_t);
  }
  va_end(ap);
  return total;
}

uint64_t sum_varargs_u32(uint32_t seed, int count, ...)
{
  uint64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    total += va_arg(ap, uint32_t);
  }
  va_end(ap);
  return total;
}

uint64_t sum_varargs_strlen(uint64_t seed, int count, ...)
{
  uint64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    total += (uint64_t)strlen(va_arg(ap, const char *));
  }
  va_end(ap);
  return total;
}

uint64_t sum_varargs_small_u8(uint64_t seed, int count, ...)
{
  uint64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    struct small_u8 v = va_arg(ap, struct small_u8);
    total += v.a;
  }
  va_end(ap);
  return total;
}

uint64_t sum_varargs_small_u16(uint64_t seed, int count, ...)
{
  uint64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    struct small_u16 v = va_arg(ap, struct small_u16);
    total += v.a;
  }
  va_end(ap);
  return total;
}

uint64_t sum_varargs_small_u32(uint64_t seed, int count, ...)
{
  uint64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    struct small_u32 v = va_arg(ap, struct small_u32);
    total += v.a;
  }
  va_end(ap);
  return total;
}

uint64_t sum_varargs_small_u64(uint64_t seed, int count, ...)
{
  uint64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    struct small_u64 v = va_arg(ap, struct small_u64);
    total += (uint64_t)v.a + (uint64_t)v.b;
  }
  va_end(ap);
  return total;
}

double sum_varargs_one_float(double seed, int count, ...)
{
  double total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    struct one_float v = va_arg(ap, struct one_float);
    total += v.a;
  }
  va_end(ap);
  return total;
}

double sum_varargs_one_float_gprseed(uint64_t seed, int count, ...)
{
  double total = (double)seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    struct one_float v = va_arg(ap, struct one_float);
    total += v.a;
  }
  va_end(ap);
  return total;
}

double sum_varargs_one_double(double seed, int count, ...)
{
  double total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    struct one_double v = va_arg(ap, struct one_double);
    total += v.a;
  }
  va_end(ap);
  return total;
}

double sum_varargs_one_double_gprseed(uint64_t seed, int count, ...)
{
  double total = (double)seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    struct one_double v = va_arg(ap, struct one_double);
    total += v.a;
  }
  va_end(ap);
  return total;
}

uint64_t sum_varargs_big_pair(uint64_t seed, int count, ...)
{
  uint64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    struct big_pair v = va_arg(ap, struct big_pair);
    total += v.a + v.b;
  }
  va_end(ap);
  return total;
}

int64_t sum_varargs_promoted_int(int32_t seed, int count, ...)
{
  int64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    total += va_arg(ap, int);
  }
  va_end(ap);
  return total;
}

double sum_varargs_float_cdata(double seed, int count, ...)
{
  double total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    total += va_arg(ap, double);
  }
  va_end(ap);
  return total;
}

double sum_varargs_complex(double seed, int count, ...)
{
  double total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    double complex v = va_arg(ap, double complex);
    total += __real__ v + __imag__ v;
  }
  va_end(ap);
  return total;
}

uint64_t sum_varargs_ptr_values(uint64_t seed, int count, ...)
{
  uint64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    int32_t *p = va_arg(ap, int32_t *);
    total += p ? (uint64_t)*p : 1000u;
  }
  va_end(ap);
  return total;
}

int64_t sum_varargs_i32_callbacks(int32_t seed, int count, ...)
{
  int64_t total = seed;
  va_list ap;
  int i;
  va_start(ap, count);
  for (i = 0; i < count; i++) {
    i32_callback fn = va_arg(ap, i32_callback);
    total += fn((int32_t)(i + 1));
  }
  va_end(ap);
  return total;
}

uint64_t boundary_mix(uint8_t a, uint16_t b, uint32_t c, uint64_t d)
{
  return (uint64_t)a + (uint64_t)b + (uint64_t)c + d;
}

uint64_t sum7_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                  uint64_t e, uint64_t f, uint64_t g)
{
  return a + b + c + d + e + f + g;
}

uint64_t sum5_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                  uint64_t e)
{
  return a + b + c + d + e;
}

uint64_t sum6_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                  uint64_t e, uint64_t f)
{
  return a + b + c + d + e + f;
}

int64_t sum7_i32(int32_t a, int32_t b, int32_t c, int32_t d,
                 int32_t e, int32_t f, int32_t g)
{
  return (int64_t)a + b + c + d + e + f + g;
}

double sum4_double(double a, double b, double c, double d)
{
  return a + b + c + d;
}

double sum5_double(double a, double b, double c, double d, double e)
{
  return a + b + c + d + e;
}

double sum6_double(double a, double b, double c, double d, double e, double f)
{
  return a + b + c + d + e + f;
}
