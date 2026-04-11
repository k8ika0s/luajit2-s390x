local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
double take_complex_sum(complex double value);
double take_complex_pair(double seed, complex double a, complex double b);
double take7_complex_sum(double seed, complex double a, complex double b,
                         complex double c, complex double d,
                         complex double e, complex double f,
                         complex double g);
double mutate_complex_arg(complex double value);
]]

local lib = ffi.load(libpath)

local z1 = ffi.new("complex double", { 1.5, 2.25 })
local z2 = ffi.new("complex double", { -3.0, 4.5 })
local z3 = ffi.new("complex double", { 1.0, 2.0 })
local z4 = ffi.new("complex double", { 3.0, 4.0 })
local z5 = ffi.new("complex double", { 5.0, 6.0 })
local z6 = ffi.new("complex double", { 7.0, 8.0 })
local z7 = ffi.new("complex double", { 9.0, 10.0 })

local function run_read(n)
  local total = 0
  for i = 1, n do
    total = total + lib.take_complex_sum(z1)
    total = total + lib.take_complex_pair(i, z1, z2)
  end
  return total
end

local function run_mut(n, z)
  local total = 0
  for _ = 1, n do
    total = total + lib.mutate_complex_arg(z)
  end
  return total
end

local function run_pressure(n)
  local total = 0
  for i = 1, n do
    total = total + lib.take7_complex_sum(i, z1, z2, z3, z4, z5, z6, z7)
  end
  return total
end

jit.off(run_read, true)
local expected_read = run_read(200)
jit.on(run_read, true)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local read_capture = t.trace_capture()
local actual_read = run_read(200)
read_capture.stop()

t.approx(actual_read, expected_read, 1e-9, "ffi fixed complex call total")
t.truthy(t.find_trace_event(read_capture.events, "stop"),
         "ffi fixed complex call traced")

jit.off(run_pressure, true)
local expected_pressure = run_pressure(200)
jit.on(run_pressure, true)

local pressure_capture = t.trace_capture()
local actual_pressure = run_pressure(200)
pressure_capture.stop()

t.approx(actual_pressure, expected_pressure, 1e-9,
         "ffi fixed complex pressure total")
t.truthy(t.find_trace_event(pressure_capture.events, "stop"),
         "ffi fixed complex pressure traced")

local zmut = ffi.new("complex double", { 3.0, 4.0 })
jit.off(run_mut, true)
local expected_mut = run_mut(20, zmut)
jit.on(run_mut, true)

zmut = ffi.new("complex double", { 3.0, 4.0 })
local mut_capture = t.trace_capture()
local actual_mut = run_mut(20, zmut)
mut_capture.stop()

t.approx(actual_mut, expected_mut, 1e-9, "ffi fixed complex mut total")
t.approx(zmut.re, 3.0, 1e-12, "ffi fixed complex mut arg.re")
t.approx(zmut.im, 4.0, 1e-12, "ffi fixed complex mut arg.im")
t.truthy(t.find_trace_event(mut_capture.events, "stop"),
         "ffi fixed complex mut traced")
