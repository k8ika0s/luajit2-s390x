local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
double sum_varargs_complex(double seed, int count, ...);
]]

local lib = ffi.load(libpath)
local z1 = ffi.new("complex double", { 1.5, 2.5 })
local z2 = ffi.new("complex double", { 3.5, 4.5 })
local z3 = ffi.new("complex double", { 5.5, 6.5 })
local z4 = ffi.new("complex double", { 7.5, 8.5 })
local z5 = ffi.new("complex double", { 9.5, 10.5 })
local z6 = ffi.new("complex double", { 11.5, 12.5 })
local z7 = ffi.new("complex double", { 13.5, 14.5 })

local function run(n)
  local total = 0
  for i = 1, n do
    total = total + lib.sum_varargs_complex(i + 0.25, 7, z1, z2, z3, z4,
                                            z5, z6, z7)
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

t.approx(actual, expected, 1e-12, "ffi complex vararg total")
t.truthy(t.find_trace_event(capture.events, "abort"), "ffi complex vararg parked")
