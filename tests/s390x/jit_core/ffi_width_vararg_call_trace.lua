local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
int64_t sum_varargs_i32(int32_t seed, int count, ...);
uint64_t sum_varargs_u32(uint32_t seed, int count, ...);
]]

local lib = ffi.load(libpath)
local i32 = ffi.typeof("int32_t")
local u32 = ffi.typeof("uint32_t")

local function run_i32(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(lib.sum_varargs_i32(i32(10), 7, i32(-1),
                                                 i32(-2000000000), i32(3),
                                                 i32(-4), i32(5), i32(-6),
                                                 i32(7)))
  end
  return total
end

local function run_u32(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(lib.sum_varargs_u32(u32(5), 7, u32(4000000000),
                                                 u32(3000000000),
                                                 u32(2000000000),
                                                 u32(1000000000), u32(17),
                                                 u32(23), u32(42)))
  end
  return total
end

jit.off(run_i32, true)
jit.off(run_u32, true)
local expected_i32 = run_i32(200)
local expected_u32 = run_u32(200)
jit.on(run_i32, true)
jit.on(run_u32, true)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local capture = t.trace_capture()
local actual_i32 = run_i32(200)
local actual_u32 = run_u32(200)
capture.stop()

t.eq(actual_i32, expected_i32, "ffi i32 vararg total")
t.eq(actual_u32, expected_u32, "ffi u32 vararg total")
t.truthy(t.find_trace_event(capture.events, "abort"), "ffi width vararg parked")
