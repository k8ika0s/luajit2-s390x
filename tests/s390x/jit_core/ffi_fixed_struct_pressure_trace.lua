local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
typedef struct { uint32_t a; } small_u32;
typedef struct { uint32_t a; uint32_t b; } small_u64;
typedef struct { double a; } one_double;

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
]]

local lib = ffi.load(libpath)

local s32 = ffi.new("small_u32", { a = 0x12345678 })
local s64 = ffi.new("small_u64", { a = 0x11111111, b = 0x22222222 })
local od = ffi.new("one_double", { a = 4.25 })

local function run_small_u32(n)
  local total = 0
  for _ = 1, n do
    total = total + tonumber(lib.take6_small_u32(s32, s32, s32, s32, s32, s32))
    total = total + tonumber(lib.take7_small_u32(s32, s32, s32, s32, s32, s32, s32))
  end
  return total
end

local function run_small_u64(n)
  local total = 0
  for _ = 1, n do
    total = total + tonumber(lib.take6_small_u64(s64, s64, s64, s64, s64, s64))
    total = total + tonumber(lib.take7_small_u64(s64, s64, s64, s64, s64, s64, s64))
  end
  return total
end

local function run_one_double(n)
  local total = 0
  for _ = 1, n do
    total = total + lib.take6_one_double(od, od, od, od, od, od)
    total = total + lib.take7_one_double(od, od, od, od, od, od, od)
  end
  return total
end

jit.off(run_small_u32, true)
jit.off(run_small_u64, true)
jit.off(run_one_double, true)
local expected_small_u32 = run_small_u32(200)
local expected_small_u64 = run_small_u64(200)
local expected_one_double = run_one_double(200)
jit.on(run_small_u32, true)
jit.on(run_small_u64, true)
jit.on(run_one_double, true)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local capture = t.trace_capture()
local actual_small_u32 = run_small_u32(200)
local actual_small_u64 = run_small_u64(200)
local actual_one_double = run_one_double(200)
capture.stop()

t.eq(actual_small_u32, expected_small_u32, "fixed struct pressure small_u32 total")
t.eq(actual_small_u64, expected_small_u64, "fixed struct pressure small_u64 total")
t.approx(actual_one_double, expected_one_double, 1e-12,
         "fixed struct pressure one_double total")
t.truthy(t.find_trace_event(capture.events, "abort"),
         "fixed struct pressure parked")
