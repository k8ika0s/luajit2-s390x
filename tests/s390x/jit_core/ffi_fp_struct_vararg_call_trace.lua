local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
typedef struct { float a; } one_float;
typedef struct { double a; } one_double;

double sum_varargs_one_float(double seed, int count, ...);
double sum_varargs_one_float_gprseed(uint64_t seed, int count, ...);
double sum_varargs_one_double(double seed, int count, ...);
double sum_varargs_one_double_gprseed(uint64_t seed, int count, ...);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")
local floats = {
  ffi.new("one_float", { a = 2.5 }),
  ffi.new("one_float", { a = 3.5 }),
  ffi.new("one_float", { a = 4.5 }),
  ffi.new("one_float", { a = 5.5 }),
  ffi.new("one_float", { a = 6.5 }),
  ffi.new("one_float", { a = 7.5 }),
}
local doubles = {
  ffi.new("one_double", { a = 3.75 }),
  ffi.new("one_double", { a = 4.75 }),
  ffi.new("one_double", { a = 5.75 }),
  ffi.new("one_double", { a = 6.75 }),
  ffi.new("one_double", { a = 7.75 }),
  ffi.new("one_double", { a = 8.75 }),
}

local function run_fp_seed(n)
  local total = 0
  for i = 1, n do
    total = total + lib.sum_varargs_one_float(i + 0.25, 5, floats[1],
                                              floats[2], floats[3],
                                              floats[4], floats[5])
    total = total + lib.sum_varargs_one_double(i + 0.5, 5, doubles[1],
                                               doubles[2], doubles[3],
                                               doubles[4], doubles[5])
  end
  return total
end

local function run_gpr_seed(n)
  local total = 0
  for i = 1, n do
    total = total + lib.sum_varargs_one_float_gprseed(u64(i), 6, floats[1],
                                                      floats[2], floats[3],
                                                      floats[4], floats[5],
                                                      floats[6])
    total = total + lib.sum_varargs_one_double_gprseed(u64(i), 6, doubles[1],
                                                       doubles[2], doubles[3],
                                                       doubles[4], doubles[5],
                                                       doubles[6])
  end
  return total
end

local function check(fn, label)
  jit.off(fn, true)
  local expected = fn(200)
  jit.on(fn, true)

  local capture = t.trace_capture()
  local actual = fn(200)
  capture.stop()

  t.approx(actual, expected, 1e-9, label .. " total")
  t.truthy(t.find_trace_event(capture.events, "stop"), label .. " traced")
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

check(run_fp_seed, "ffi fp-seed fp struct vararg")
check(run_gpr_seed, "ffi gpr-seed fp struct vararg")
