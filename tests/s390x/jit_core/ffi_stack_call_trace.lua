local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
uint64_t sum7_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                  uint64_t e, uint64_t f, uint64_t g);
int64_t sum7_i32(int32_t a, int32_t b, int32_t c, int32_t d,
                 int32_t e, int32_t f, int32_t g);
double sum6_double(double a, double b, double c, double d, double e, double f);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")

local function run_gpr(n)
  local total = u64(0)
  for i = 1, n do
    total = total + lib.sum7_u64(u64(i), u64(2), u64(3), u64(4),
                                 u64(5), u64(6), u64(7))
  end
  return total
end

local function run_i32(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(lib.sum7_i32(i, -2, 3, -4, 5, -6, 7))
  end
  return total
end

local function run_fpr(n)
  local total = 0
  for i = 1, n do
    total = total + lib.sum6_double(i + 0.25, 2.5, 3.75, 4.5, 5.25, 6.75)
  end
  return total
end

jit.off(run_gpr, true)
jit.off(run_i32, true)
jit.off(run_fpr, true)
local expected_gpr = run_gpr(200)
local expected_i32 = run_i32(200)
local expected_fpr = run_fpr(200)
jit.on(run_gpr, true)
jit.on(run_i32, true)
jit.on(run_fpr, true)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local capture = t.trace_capture()
local actual_gpr = run_gpr(200)
local actual_i32 = run_i32(200)
local actual_fpr = run_fpr(200)
capture.stop()

t.eq(tonumber(actual_gpr), tonumber(expected_gpr), "stack gpr call total")
t.eq(actual_i32, expected_i32, "stack i32 call total")
t.approx(actual_fpr, expected_fpr, 1e-12, "stack fpr call total")
t.truthy(t.find_trace_event(capture.events, "stop"), "ffi stack call traced")
