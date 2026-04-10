local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
double sum_varargs_double(double seed, int count, ...);
]]

local lib = ffi.load(libpath)

local function run(n)
  local total = 0
  for i = 1, n do
    total = total + lib.sum_varargs_double(i + 0.25, 6, 2.5, 3.75,
                                           4.5, 5.25, 6.75, 7.0)
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

t.approx(actual, expected, 1e-12, "ffi fp vararg total")
t.truthy(t.find_trace_event(capture.events, "stop"), "ffi fp vararg traced")
