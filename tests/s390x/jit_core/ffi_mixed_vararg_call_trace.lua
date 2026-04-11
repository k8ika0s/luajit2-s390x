local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
double sum_varargs_mixed(uint64_t seed, int pairs, ...);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")

local function run(n)
  local total = 0
  for i = 1, n do
    total = total + lib.sum_varargs_mixed(u64(i), 6, u64(10), 1.5,
                                          u64(20), 2.5, u64(30), 3.5,
                                          u64(40), 4.5, u64(50), 5.5,
                                          u64(60), 6.5)
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

t.approx(actual, expected, 1e-12, "ffi mixed vararg total")
t.truthy(t.find_trace_event(capture.events, "abort"), "ffi mixed vararg parked")
