local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
typedef struct { uint8_t a; } small_u8;
typedef struct { uint16_t a; } small_u16;
typedef struct { uint32_t a; } small_u32;
typedef struct { uint32_t a; uint32_t b; } small_u64;
typedef struct { float a; } one_float;
typedef struct { double a; } one_double;
typedef struct { uint64_t a; uint64_t b; } big_pair;
typedef struct { double a; double b; } hfa2d;

uint64_t take_small_u8(small_u8 value);
uint64_t take_small_u16(small_u16 value);
uint64_t take_small_u32(small_u32 value);
uint64_t take_small_u64(small_u64 value);
double take_one_float(one_float value);
double take_one_double(one_double value);
uint64_t take_big_pair(big_pair value);
double take_hfa2d(hfa2d value);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")

local s8 = ffi.new("small_u8", { a = 17 })
local s16 = ffi.new("small_u16", { a = 4095 })
local s32 = ffi.new("small_u32", { a = 0x12345678 })
local s64 = ffi.new("small_u64", { a = 0x11111111, b = 0x22222222 })
local sf = ffi.new("one_float", { a = 3.5 })
local sd = ffi.new("one_double", { a = 4.25 })
local big = ffi.new("big_pair", { a = u64(7000), b = u64(8000) })
local hfa = ffi.new("hfa2d", { a = 1.25, b = 2.5 })

local function run(n)
  local total = 0
  for _ = 1, n do
    total = total + tonumber(lib.take_small_u8(s8))
    total = total + tonumber(lib.take_small_u16(s16))
    total = total + tonumber(lib.take_small_u32(s32))
    total = total + tonumber(lib.take_small_u64(s64))
    total = total + lib.take_one_float(sf)
    total = total + lib.take_one_double(sd)
    total = total + tonumber(lib.take_big_pair(big))
    total = total + lib.take_hfa2d(hfa)
  end
  return total
end

jit.off(run, true)
local expected = run(200)
jit.on(run, true)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local capture = t.trace_capture()
local actual = run(200)
capture.stop()

t.approx(actual, expected, 1e-6, "ffi fixed struct call total")
t.truthy(t.find_trace_event(capture.events, "abort"), "ffi fixed struct call parked")
