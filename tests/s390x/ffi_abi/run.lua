local ffi = require("ffi")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"
local function u64(value)
  return ffi.new("uint64_t", value)
end

local function i64(value)
  return ffi.new("int64_t", value)
end

ffi.cdef([[
typedef struct { uint8_t a; } small_u8;
typedef struct { uint16_t a; } small_u16;
typedef struct { uint32_t a; } small_u32;
typedef struct { uint32_t a; uint32_t b; } small_u64;
typedef struct { float a; } one_float;
typedef struct { double a; } one_double;
typedef struct { uint64_t a; uint64_t b; } big_pair;
typedef struct { double a; double b; } hfa2d;
typedef enum { PROBE_RED = 11, PROBE_GREEN = 13, PROBE_BLUE = 17 } probe_color;
typedef int32_t (*i32_callback)(int32_t);

int8_t echo_i8(int8_t value);
uint8_t echo_u8(uint8_t value);
int16_t echo_i16(int16_t value);
uint16_t echo_u16(uint16_t value);
int32_t echo_i32(int32_t value);
uint32_t echo_u32(uint32_t value);
int64_t echo_i64(int64_t value);
uint64_t echo_u64(uint64_t value);

float add_float(float a, float b);
double add_double(double a, double b);
complex double add_complex(complex double a, complex double b);
complex double mul_complex(complex double a, complex double b);
double take_complex_sum(complex double value);
double take_complex_pair(double seed, complex double a, complex double b);
double mutate_complex_arg(complex double value);

small_u8 echo_small_u8(small_u8 value);
small_u16 echo_small_u16(small_u16 value);
small_u32 echo_small_u32(small_u32 value);
small_u64 echo_small_u64(small_u64 value);
big_pair echo_big_pair(big_pair value);
hfa2d echo_hfa2d(hfa2d value);

uint64_t take_small_u8(small_u8 value);
uint64_t take_small_u16(small_u16 value);
uint64_t take_small_u32(small_u32 value);
uint64_t take_small_u64(small_u64 value);
double take_one_float(one_float value);
double take_one_double(one_double value);
uint64_t take_big_pair(big_pair value);
double take_hfa2d(hfa2d value);
uint64_t take6_small_u32(small_u32 a, small_u32 b, small_u32 c,
                         small_u32 d, small_u32 e, small_u32 f);
uint64_t take7_small_u32(small_u32 a, small_u32 b, small_u32 c,
                         small_u32 d, small_u32 e, small_u32 f,
                         small_u32 g);
uint64_t take6_small_u64(small_u64 a, small_u64 b, small_u64 c,
                         small_u64 d, small_u64 e, small_u64 f);
uint64_t take7_small_u64(small_u64 a, small_u64 b, small_u64 c,
                         small_u64 d, small_u64 e, small_u64 f,
                         small_u64 g);
double take6_one_double(one_double a, one_double b, one_double c,
                        one_double d, one_double e, one_double f);
double take7_one_double(one_double a, one_double b, one_double c,
                        one_double d, one_double e, one_double f,
                        one_double g);

uint64_t sum_varargs(uint64_t seed, int count, ...);
double sum_varargs_double(double seed, int count, ...);
double sum_varargs_mixed(uint64_t seed, int pairs, ...);
int64_t sum_varargs_i32(int32_t seed, int count, ...);
uint64_t sum_varargs_u32(uint32_t seed, int count, ...);
uint64_t sum_varargs_strlen(uint64_t seed, int count, ...);
uint64_t sum_varargs_small_u8(uint64_t seed, int count, ...);
uint64_t sum_varargs_small_u16(uint64_t seed, int count, ...);
uint64_t sum_varargs_small_u32(uint64_t seed, int count, ...);
uint64_t sum_varargs_small_u64(uint64_t seed, int count, ...);
double sum_varargs_one_float(double seed, int count, ...);
double sum_varargs_one_float_gprseed(uint64_t seed, int count, ...);
double sum_varargs_one_double(double seed, int count, ...);
double sum_varargs_one_double_gprseed(uint64_t seed, int count, ...);
uint64_t sum_varargs_big_pair(uint64_t seed, int count, ...);
int64_t sum_varargs_promoted_int(int32_t seed, int count, ...);
double sum_varargs_float_cdata(double seed, int count, ...);
double sum_varargs_complex(double seed, int count, ...);
uint64_t sum_varargs_ptr_values(uint64_t seed, int count, ...);
int64_t sum_varargs_i32_callbacks(int32_t seed, int count, ...);
uint64_t boundary_mix(uint8_t a, uint16_t b, uint32_t c, uint64_t d);
uint64_t sum7_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                  uint64_t e, uint64_t f, uint64_t g);
int64_t sum7_i32(int32_t a, int32_t b, int32_t c, int32_t d,
                 int32_t e, int32_t f, int32_t g);
double sum6_double(double a, double b, double c, double d, double e, double f);
]])

local lib = ffi.load(libpath)

t.eq(lib.echo_i8(-7), -7, "echo_i8")
t.eq(lib.echo_u8(255), 255, "echo_u8")
t.eq(lib.echo_i16(-1024), -1024, "echo_i16")
t.eq(lib.echo_u16(65535), 65535, "echo_u16")
t.eq(lib.echo_i32(-2000000000), -2000000000, "echo_i32")
t.eq(tonumber(lib.echo_u32(ffi.new("uint32_t", 4000000000))), 4000000000, "echo_u32")
t.eq(tonumber(lib.echo_i64(i64(-9000000000000))), -9000000000000, "echo_i64")
t.eq(tonumber(lib.echo_u64(u64(9000000000000))), 9000000000000, "echo_u64")

t.approx(lib.add_float(1.5, 2.25), 3.75, 1e-6, "add_float")
t.approx(lib.add_double(1.5, 2.25), 3.75, 1e-12, "add_double")
local z1 = ffi.new("complex double", { 1.5, -2.25 })
local z2 = ffi.new("complex double", { -0.5, 0.75 })
local zsum = lib.add_complex(z1, z2)
t.approx(zsum.re, 1.0, 1e-12, "add_complex.re")
t.approx(zsum.im, -1.5, 1e-12, "add_complex.im")
local zmul = lib.mul_complex(z1, z2)
t.approx(zmul.re, 0.9375, 1e-12, "mul_complex.re")
t.approx(zmul.im, 2.25, 1e-12, "mul_complex.im")
t.approx(lib.take_complex_sum(z1), -21.0, 1e-12, "take_complex_sum")
t.approx(lib.take_complex_pair(2.0, z1, z2), -0.5, 1e-12,
         "take_complex_pair")
local zmut = ffi.new("complex double", { 3.0, 4.0 })
t.approx(lib.mutate_complex_arg(zmut), 38.0, 1e-12, "mutate_complex_arg")
t.approx(zmut.re, 3.0, 1e-12, "mutate_complex_arg.re")
t.approx(zmut.im, 4.0, 1e-12, "mutate_complex_arg.im")

local s8 = ffi.new("small_u8", { a = 17 })
t.eq(lib.echo_small_u8(s8).a, 17, "small_u8")

local s16 = ffi.new("small_u16", { a = 4095 })
t.eq(lib.echo_small_u16(s16).a, 4095, "small_u16")

local s32 = ffi.new("small_u32", { a = 0x12345678 })
t.eq(tonumber(lib.echo_small_u32(s32).a), 0x12345678, "small_u32")

local s64 = ffi.new("small_u64", { a = 0x11111111, b = 0x22222222 })
local s64_out = lib.echo_small_u64(s64)
t.eq(tonumber(s64_out.a), 0x11111111, "small_u64.a")
t.eq(tonumber(s64_out.b), 0x22222222, "small_u64.b")

local big = ffi.new("big_pair", { a = u64(7000000000000), b = u64(8000000000000) })
local big_out = lib.echo_big_pair(big)
t.eq(tonumber(big_out.a), tonumber(big.a), "big_pair.a")
t.eq(tonumber(big_out.b), tonumber(big.b), "big_pair.b")

local hfa = ffi.new("hfa2d", { a = 1.25, b = 2.5 })
local hfa_out = lib.echo_hfa2d(hfa)
t.approx(hfa_out.a, 1.25, 1e-12, "hfa2d.a")
t.approx(hfa_out.b, 2.5, 1e-12, "hfa2d.b")

t.eq(tonumber(lib.take_small_u8(s8)), 17, "take_small_u8")
t.eq(tonumber(lib.take_small_u16(s16)), 4095, "take_small_u16")
t.eq(tonumber(lib.take_small_u32(s32)), 0x12345678, "take_small_u32")
t.eq(tonumber(lib.take_small_u64(s64)), 0x33333333, "take_small_u64")
t.approx(lib.take_one_float(ffi.new("one_float", { a = 3.5 })), 3.5,
         1e-6, "take_one_float")
t.approx(lib.take_one_double(ffi.new("one_double", { a = 4.25 })), 4.25,
         1e-12, "take_one_double")
t.eq(tonumber(lib.take_big_pair(big)), tonumber(big.a + big.b),
     "take_big_pair")
t.approx(lib.take_hfa2d(hfa), 3.75, 1e-12, "take_hfa2d")
t.eq(tonumber(lib.take6_small_u32(s32, s32, s32, s32, s32, s32)),
     6 * 0x12345678, "take6_small_u32")
t.eq(tonumber(lib.take7_small_u32(s32, s32, s32, s32, s32, s32, s32)),
     7 * 0x12345678, "take7_small_u32")
t.eq(tonumber(lib.take6_small_u64(s64, s64, s64, s64, s64, s64)),
     6 * 0x33333333, "take6_small_u64")
t.eq(tonumber(lib.take7_small_u64(s64, s64, s64, s64, s64, s64, s64)),
     7 * 0x33333333, "take7_small_u64")
local od = ffi.new("one_double", { a = 4.25 })
t.approx(lib.take6_one_double(od, od, od, od, od, od), 6 * 4.25,
         1e-12, "take6_one_double")
t.approx(lib.take7_one_double(od, od, od, od, od, od, od), 7 * 4.25,
         1e-12, "take7_one_double")

t.eq(tonumber(lib.sum_varargs(u64(10), 4, u64(1), u64(2), u64(3), u64(4))), 20, "sum_varargs")
t.approx(lib.sum_varargs_double(1.25, 6, 2.5, 3.75, 4.5, 5.25, 6.75, 7.0), 31.0, 1e-12, "sum_varargs_double")
t.approx(lib.sum_varargs_mixed(u64(1), 6, u64(10), 1.5, u64(20), 2.5,
                               u64(30), 3.5, u64(40), 4.5, u64(50), 5.5,
                               u64(60), 6.5), 235.0, 1e-12,
         "sum_varargs_mixed")
t.eq(tonumber(lib.sum_varargs_i32(10, 7, ffi.new("int32_t", -1),
                                  ffi.new("int32_t", -2000000000),
                                  ffi.new("int32_t", 3),
                                  ffi.new("int32_t", -4),
                                  ffi.new("int32_t", 5),
                                  ffi.new("int32_t", -6),
                                  ffi.new("int32_t", 7))),
     -1999999986, "sum_varargs_i32")
t.eq(tonumber(lib.sum_varargs_u32(ffi.new("uint32_t", 5), 7,
                                  ffi.new("uint32_t", 4000000000),
                                  ffi.new("uint32_t", 3000000000),
                                  ffi.new("uint32_t", 2000000000),
                                  ffi.new("uint32_t", 1000000000),
                                  ffi.new("uint32_t", 17),
                                  ffi.new("uint32_t", 23),
                                  ffi.new("uint32_t", 42))),
     10000000087, "sum_varargs_u32")
t.eq(tonumber(lib.sum_varargs_strlen(u64(10), 7, "a", "bb", "ccc", "dddd",
                                     "eeeee", "ffffff", "ggggggg")),
     38, "sum_varargs_strlen")
t.eq(tonumber(lib.sum_varargs_small_u8(u64(10), 7,
                                       ffi.new("small_u8", { a = 1 }),
                                       ffi.new("small_u8", { a = 2 }),
                                       ffi.new("small_u8", { a = 3 }),
                                       ffi.new("small_u8", { a = 4 }),
                                       ffi.new("small_u8", { a = 5 }),
                                       ffi.new("small_u8", { a = 6 }),
                                       ffi.new("small_u8", { a = 7 }))),
     38, "sum_varargs_small_u8")
t.eq(tonumber(lib.sum_varargs_small_u16(u64(10), 7,
                                        ffi.new("small_u16", { a = 100 }),
                                        ffi.new("small_u16", { a = 200 }),
                                        ffi.new("small_u16", { a = 300 }),
                                        ffi.new("small_u16", { a = 400 }),
                                        ffi.new("small_u16", { a = 500 }),
                                        ffi.new("small_u16", { a = 600 }),
                                        ffi.new("small_u16", { a = 700 }))),
     2810, "sum_varargs_small_u16")
t.eq(tonumber(lib.sum_varargs_small_u32(u64(10), 7,
                                        ffi.new("small_u32", { a = 1000 }),
                                        ffi.new("small_u32", { a = 2000 }),
                                        ffi.new("small_u32", { a = 3000 }),
                                        ffi.new("small_u32", { a = 4000 }),
                                        ffi.new("small_u32", { a = 5000 }),
                                        ffi.new("small_u32", { a = 6000 }),
                                        ffi.new("small_u32", { a = 7000 }))),
     28010, "sum_varargs_small_u32")
t.eq(tonumber(lib.sum_varargs_small_u64(u64(10), 7,
                                        ffi.new("small_u64", { a = 1, b = 10 }),
                                        ffi.new("small_u64", { a = 2, b = 20 }),
                                        ffi.new("small_u64", { a = 3, b = 30 }),
                                        ffi.new("small_u64", { a = 4, b = 40 }),
                                        ffi.new("small_u64", { a = 5, b = 50 }),
                                        ffi.new("small_u64", { a = 6, b = 60 }),
                                        ffi.new("small_u64", { a = 7, b = 70 }))),
     318, "sum_varargs_small_u64")
t.approx(lib.sum_varargs_one_float(1.25, 5,
                                   ffi.new("one_float", { a = 2.5 }),
                                   ffi.new("one_float", { a = 2.5 }),
                                   ffi.new("one_float", { a = 2.5 }),
                                   ffi.new("one_float", { a = 2.5 }),
                                   ffi.new("one_float", { a = 2.5 })),
         13.75, 1e-6, "sum_varargs_one_float")
t.approx(lib.sum_varargs_one_float_gprseed(u64(10), 6,
                                           ffi.new("one_float", { a = 2.5 }),
                                           ffi.new("one_float", { a = 3.5 }),
                                           ffi.new("one_float", { a = 4.5 }),
                                           ffi.new("one_float", { a = 5.5 }),
                                           ffi.new("one_float", { a = 6.5 }),
                                           ffi.new("one_float", { a = 7.5 })),
         40.0, 1e-6, "sum_varargs_one_float_gprseed")
t.approx(lib.sum_varargs_one_double(1.25, 5,
                                    ffi.new("one_double", { a = 3.75 }),
                                    ffi.new("one_double", { a = 3.75 }),
                                    ffi.new("one_double", { a = 3.75 }),
                                    ffi.new("one_double", { a = 3.75 }),
                                    ffi.new("one_double", { a = 3.75 })),
         20.0, 1e-12, "sum_varargs_one_double")
t.approx(lib.sum_varargs_one_double_gprseed(u64(10), 6,
                                            ffi.new("one_double", { a = 3.75 }),
                                            ffi.new("one_double", { a = 4.75 }),
                                            ffi.new("one_double", { a = 5.75 }),
                                            ffi.new("one_double", { a = 6.75 }),
                                            ffi.new("one_double", { a = 7.75 }),
                                            ffi.new("one_double", { a = 8.75 })),
         47.5, 1e-12, "sum_varargs_one_double_gprseed")
t.eq(tonumber(lib.sum_varargs_big_pair(u64(10), 4,
                                       ffi.new("big_pair", { a = u64(1), b = u64(10) }),
                                       ffi.new("big_pair", { a = u64(2), b = u64(20) }),
                                       ffi.new("big_pair", { a = u64(3), b = u64(30) }),
                                       ffi.new("big_pair", { a = u64(4), b = u64(40) }))),
     120, "sum_varargs_big_pair")
t.eq(tonumber(lib.sum_varargs_promoted_int(10, 7,
                                           ffi.new("int8_t", -1),
                                           ffi.new("uint8_t", 250),
                                           ffi.new("int16_t", -2000),
                                           ffi.new("uint16_t", 60000),
                                           true,
                                           ffi.cast("probe_color", 11),
                                           ffi.cast("probe_color", 17))),
     58288, "sum_varargs_promoted_int")
t.approx(lib.sum_varargs_float_cdata(1.25, 4, ffi.new("float", 2.5),
                                     ffi.new("float", 3.75),
                                     ffi.new("float", 4.5),
                                     ffi.new("float", 5.25)),
         17.25, 1e-12, "sum_varargs_float_cdata")
t.approx(lib.sum_varargs_complex(10, 2,
                                 ffi.new("complex double", { 1.5, 2.5 }),
                                 ffi.new("complex double", { 3.5, 4.5 })),
         22.0, 1e-12, "sum_varargs_complex")
do
  local a = ffi.new("int32_t[1]", { 7 })
  local b = ffi.new("int32_t[1]", { 11 })
  local c = ffi.new("int32_t[1]", { 13 })
  t.eq(tonumber(lib.sum_varargs_ptr_values(u64(10), 4, a,
                                           ffi.cast("int32_t *", b),
                                           nil, c)),
       1041, "sum_varargs_ptr_values")
end
t.eq(tonumber(lib.sum_varargs_i32_callbacks(10, 3, lib.echo_i32, lib.echo_i32,
                                            lib.echo_i32)),
     16, "sum_varargs_i32_callbacks")
t.eq(tonumber(lib.boundary_mix(1, 2, 3, u64(4))), 10, "boundary_mix")
t.eq(tonumber(lib.sum7_u64(u64(1), u64(2), u64(3), u64(4), u64(5), u64(6), u64(7))), 28, "sum7_u64")
t.eq(tonumber(lib.sum7_i32(1, -2, 3, -4, 5, -6, 7)), 4, "sum7_i32")
t.approx(lib.sum6_double(1.25, 2.5, 3.75, 4.5, 5.25, 6.75), 24.0, 1e-12, "sum6_double")
