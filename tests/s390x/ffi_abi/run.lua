local ffi = require("ffi")
local t = require("tests.s390x.helpers.testlib")

local libpath = assert(arg[1], "missing oracle library path")
local function u64(value)
  return ffi.new("uint64_t", value)
end

local function i64(value)
  return ffi.new("int64_t", value)
end

ffi.cdef([[
typedef struct { uint8_t a; } small_u8;
typedef struct { uint16_t a; } small_u16;
typedef struct { uint32_t a; uint32_t b; } small_u64;
typedef struct { uint64_t a; uint64_t b; } big_pair;
typedef struct { double a; double b; } hfa2d;

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

small_u8 echo_small_u8(small_u8 value);
small_u16 echo_small_u16(small_u16 value);
small_u64 echo_small_u64(small_u64 value);
big_pair echo_big_pair(big_pair value);
hfa2d echo_hfa2d(hfa2d value);

uint64_t sum_varargs(uint64_t seed, int count, ...);
uint64_t boundary_mix(uint8_t a, uint16_t b, uint32_t c, uint64_t d);
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

local s8 = ffi.new("small_u8", { a = 17 })
t.eq(lib.echo_small_u8(s8).a, 17, "small_u8")

local s16 = ffi.new("small_u16", { a = 4095 })
t.eq(lib.echo_small_u16(s16).a, 4095, "small_u16")

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

t.eq(tonumber(lib.sum_varargs(u64(10), 4, u64(1), u64(2), u64(3), u64(4))), 20, "sum_varargs")
t.eq(tonumber(lib.boundary_mix(1, 2, 3, u64(4))), 10, "boundary_mix")
