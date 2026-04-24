local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
float take_complexf_sum(complex float value);
float take_complexf_pair(float seed, complex float a, complex float b);
float mutate_complexf_arg(complex float value);
double take_complex_sum(complex double value);
double take_complex_pair(double seed, complex double a, complex double b);
double mutate_complex_arg(complex double value);
]]

local lib = ffi.load(libpath)

local zf1 = ffi.new("complex float", { 1.5, 2.25 })
local zf2 = ffi.new("complex float", { -3.0, 4.5 })
local z1 = ffi.new("complex double", { 1.5, 2.25 })
local z2 = ffi.new("complex double", { -3.0, 4.5 })

local function run_readf(n)
  local total = 0
  for i = 1, n do
    total = total + lib.take_complexf_sum(zf1)
    total = total + lib.take_complexf_pair(i, zf1, zf2)
  end
  return total
end

local function run_read(n)
  local total = 0
  for i = 1, n do
    total = total + lib.take_complex_sum(z1)
    total = total + lib.take_complex_pair(i, z1, z2)
  end
  return total
end

local function run_mutf(n, z)
  local total = 0
  for _ = 1, n do
    total = total + lib.mutate_complexf_arg(z)
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

jit.off(run_readf, true)
local expected_readf = run_readf(200)
jit.on(run_readf, true)

jit.off(run_read, true)
local expected_read = run_read(200)
jit.on(run_read, true)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local readf_capture = t.trace_capture()
local actual_readf = run_readf(200)
readf_capture.stop()

t.approx(actual_readf, expected_readf, 1e-5, "ffi fixed complexf call total")

local read_capture = t.trace_capture()
local actual_read = run_read(200)
read_capture.stop()

t.approx(actual_read, expected_read, 1e-9, "ffi fixed complex call total")
t.truthy(t.find_trace_event(read_capture.events, "stop"),
         "ffi fixed complex call traced")

local zfmut = ffi.new("complex float", { 3.0, 4.0 })
jit.off(run_mutf, true)
local expected_mutf = run_mutf(20, zfmut)
jit.on(run_mutf, true)

zfmut = ffi.new("complex float", { 3.0, 4.0 })
local mutf_capture = t.trace_capture()
local actual_mutf = run_mutf(20, zfmut)
mutf_capture.stop()

t.approx(actual_mutf, expected_mutf, 1e-5, "ffi fixed complexf mut total")
t.approx(zfmut.re, 3.0, 1e-6, "ffi fixed complexf mut arg.re")
t.approx(zfmut.im, 4.0, 1e-6, "ffi fixed complexf mut arg.im")

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
