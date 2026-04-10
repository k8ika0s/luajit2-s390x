local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
uint64_t echo_u64(uint64_t value);
uint64_t sum7_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                  uint64_t e, uint64_t f, uint64_t g);
double add_double(double a, double b);
double sum6_double(double a, double b, double c, double d, double e, double f);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")

local function run_gpr(n)
  local total = u64(0)
  for i = 1, n do
    local a = lib.echo_u64(u64(i))
    local b = lib.echo_u64(u64(i + 1))
    local c = lib.echo_u64(u64(i + 2))
    local d = lib.echo_u64(u64(i + 3))
    total = total + lib.sum7_u64(a, b, c, d, a, b, c)
  end
  return total
end

local function run_fpr(n)
  local total = 0
  for i = 1, n do
    local a = lib.add_double(i + 0.25, 0)
    local b = lib.add_double(i + 1.5, 0)
    local c = lib.add_double(i + 2.75, 0)
    total = total + lib.sum6_double(a, b, c, a, b, c)
  end
  return total
end

jit.off(run_gpr, true)
jit.off(run_fpr, true)
local expected_gpr = run_gpr(200)
local expected_fpr = run_fpr(200)
jit.on(run_gpr, true)
jit.on(run_fpr, true)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local capture = t.trace_capture()
local actual_gpr = run_gpr(200)
local actual_fpr = run_fpr(200)
capture.stop()

t.eq(tonumber(actual_gpr), tonumber(expected_gpr), "fixed call pressure gpr total")
t.approx(actual_fpr, expected_fpr, 1e-12, "fixed call pressure fpr total")
t.truthy(t.find_trace_event(capture.events, "stop"), "fixed call pressure traced")
