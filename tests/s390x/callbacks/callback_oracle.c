#include <stdint.h>

typedef int (*int_cb_t)(int);
typedef int (*sum6_cb_t)(int, int, int, int, int, int);
typedef double (*mix_cb_t)(double, double, int, double, int);
typedef uint64_t (*u64_cb_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

int call_once(int_cb_t cb, int value)
{
  return cb(value);
}

int call_many(int_cb_t cb, int start, int count)
{
  int total = 0;
  int i;
  for (i = 0; i < count; i++) {
    total += cb(start + i);
  }
  return total;
}

int call_recursive(int_cb_t cb, int depth)
{
  if (depth <= 0) {
    return 0;
  }
  return cb(depth) + call_recursive(cb, depth - 1);
}

int call_sum6(sum6_cb_t cb, int a, int b, int c, int d, int e, int f)
{
  return cb(a, b, c, d, e, f);
}

double call_mix(mix_cb_t cb, double a, double b, int c, double d, int e)
{
  return cb(a, b, c, d, e);
}

uint64_t call_u64(u64_cb_t cb, uint64_t a, uint64_t b, uint64_t c,
                  uint64_t d, uint64_t e, uint64_t f)
{
  return cb(a, b, c, d, e, f);
}
