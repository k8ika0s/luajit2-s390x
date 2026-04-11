local ffi = require("ffi")
local bench = dofile("tests/s390x/perf/benchlib.lua")

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

local take_complex_sum = lib.take_complex_sum
local take_complex_pair = lib.take_complex_pair
local take7_complex_sum = lib.take7_complex_sum
local mutate_complex_arg = lib.mutate_complex_arg

local scales = {
  small = 4000,
  medium = 20000,
  hot = 80000,
}

local function complex_sum_call(n)
  local total = 0
  for _ = 1, n do
    total = total + take_complex_sum(z1)
  end
  return total
end

local function complex_pair_call(n)
  local total = 0
  for i = 1, n do
    total = total + take_complex_pair(i, z1, z2)
  end
  return total
end

local function complex_take7_call(n)
  local total = 0
  for i = 1, n do
    total = total + take7_complex_sum(i, z1, z2, z3, z4, z5, z6, z7)
  end
  return total
end

local function complex_mut_call(n)
  local total = 0
  local zmut = ffi.new("complex double", { 3.0, 4.0 })
  for _ = 1, n do
    total = total + mutate_complex_arg(zmut)
  end
  return total
end

local workloads = {
  { "complex_sum_call", complex_sum_call },
  { "complex_pair_call", complex_pair_call },
  { "complex_take7_call", complex_take7_call },
  { "complex_mut_call", complex_mut_call },
}

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  for _, workload in ipairs(workloads) do
    local name = workload[1]
    local run = workload[2]
    local expected = run(n)
    cases[#cases + 1] = {
      workload = name,
      scale = scale,
      iterations = n,
      run = run,
      validate = function(result)
        bench.approx(result, expected, 1e-6, name .. "/" .. scale)
      end,
    }
  end
end

bench.run_suite({ family = "ffi_fixed_complex_calls", cases = cases })
