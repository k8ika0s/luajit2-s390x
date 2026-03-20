#include <stdint.h>
#include <stdarg.h>
#include <complex.h>

struct small_u8 {
  uint8_t a;
};

struct small_u16 {
  uint16_t a;
};

struct small_u64 {
  uint32_t a;
  uint32_t b;
};

struct big_pair {
  uint64_t a;
  uint64_t b;
};

struct hfa2d {
  double a;
  double b;
};

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
double complex add_complex(double complex a, double complex b) { return a + b; }
double complex mul_complex(double complex a, double complex b) { return a * b; }

struct small_u8 echo_small_u8(struct small_u8 value) { return value; }
struct small_u16 echo_small_u16(struct small_u16 value) { return value; }
struct small_u64 echo_small_u64(struct small_u64 value) { return value; }
struct big_pair echo_big_pair(struct big_pair value) { return value; }
struct hfa2d echo_hfa2d(struct hfa2d value) { return value; }

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

uint64_t boundary_mix(uint8_t a, uint16_t b, uint32_t c, uint64_t d)
{
  return (uint64_t)a + (uint64_t)b + (uint64_t)c + d;
}
