local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
typedef struct { uint8_t a; } small_u8;
typedef struct { uint16_t a; } small_u16;
typedef struct { uint32_t a; } small_u32;
typedef struct { uint32_t a; uint32_t b; } small_u64;

uint64_t sum_varargs_small_u8(uint64_t seed, int count, ...);
uint64_t sum_varargs_small_u16(uint64_t seed, int count, ...);
uint64_t sum_varargs_small_u32(uint64_t seed, int count, ...);
uint64_t sum_varargs_small_u64(uint64_t seed, int count, ...);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")

local u8 = {
  ffi.new("small_u8", { a = 1 }), ffi.new("small_u8", { a = 2 }),
  ffi.new("small_u8", { a = 3 }), ffi.new("small_u8", { a = 4 }),
  ffi.new("small_u8", { a = 5 }), ffi.new("small_u8", { a = 6 }),
  ffi.new("small_u8", { a = 7 }),
}
local u16 = {
  ffi.new("small_u16", { a = 100 }), ffi.new("small_u16", { a = 200 }),
  ffi.new("small_u16", { a = 300 }), ffi.new("small_u16", { a = 400 }),
  ffi.new("small_u16", { a = 500 }), ffi.new("small_u16", { a = 600 }),
  ffi.new("small_u16", { a = 700 }),
}
local u32 = {
  ffi.new("small_u32", { a = 1000 }), ffi.new("small_u32", { a = 2000 }),
  ffi.new("small_u32", { a = 3000 }), ffi.new("small_u32", { a = 4000 }),
  ffi.new("small_u32", { a = 5000 }), ffi.new("small_u32", { a = 6000 }),
  ffi.new("small_u32", { a = 7000 }),
}
local u64s = {
  ffi.new("small_u64", { a = 1, b = 10 }),
  ffi.new("small_u64", { a = 2, b = 20 }),
  ffi.new("small_u64", { a = 3, b = 30 }),
  ffi.new("small_u64", { a = 4, b = 40 }),
  ffi.new("small_u64", { a = 5, b = 50 }),
  ffi.new("small_u64", { a = 6, b = 60 }),
  ffi.new("small_u64", { a = 7, b = 70 }),
}

local function run(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(lib.sum_varargs_small_u8(u64(i), 7, u8[1], u8[2],
                                                      u8[3], u8[4], u8[5],
                                                      u8[6], u8[7]))
    total = total + tonumber(lib.sum_varargs_small_u16(u64(i), 7, u16[1],
                                                       u16[2], u16[3],
                                                       u16[4], u16[5],
                                                       u16[6], u16[7]))
    total = total + tonumber(lib.sum_varargs_small_u32(u64(i), 7, u32[1],
                                                       u32[2], u32[3],
                                                       u32[4], u32[5],
                                                       u32[6], u32[7]))
    total = total + tonumber(lib.sum_varargs_small_u64(u64(i), 7, u64s[1],
                                                       u64s[2], u64s[3],
                                                       u64s[4], u64s[5],
                                                       u64s[6], u64s[7]))
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

t.eq(actual, expected, "ffi small struct vararg total")
t.truthy(t.find_trace_event(capture.events, "stop"), "ffi small struct vararg traced")
