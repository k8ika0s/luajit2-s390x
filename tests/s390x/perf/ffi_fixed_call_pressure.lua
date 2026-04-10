local ffi = require("ffi")
local bench = dofile("tests/s390x/perf/benchlib.lua")

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

local function gpr_pressure(n)
  local total = u64(0)
  for i = 1, n do
    local a = lib.echo_u64(u64(i))
    local b = lib.echo_u64(u64(i + 1))
    local c = lib.echo_u64(u64(i + 2))
    local d = lib.echo_u64(u64(i + 3))
    total = total + lib.sum7_u64(a, b, c, d, a, b, c)
  end
  return tonumber(total)
end

local function fpr_pressure(n)
  local total = 0
  for i = 1, n do
    local a = lib.add_double(i + 0.25, 0)
    local b = lib.add_double(i + 1.5, 0)
    local c = lib.add_double(i + 2.75, 0)
    total = total + lib.sum6_double(a, b, c, a, b, c)
  end
  return total
end

local scales = {
  small = 1000,
  medium = 5000,
  hot = 20000,
}

local workloads = {
  { "gpr_pressure", gpr_pressure },
  { "fpr_pressure", fpr_pressure },
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
        if name == "gpr_pressure" then
          bench.eq(result, expected, name .. "/" .. scale)
        elseif math.abs(result - expected) > 1e-9 then
          error(name .. "/" .. scale .. ": expected " .. tostring(expected) ..
                ", got " .. tostring(result))
        end
      end,
    }
  end
end

bench.run_suite({ family = "ffi_fixed_call_pressure", cases = cases })
