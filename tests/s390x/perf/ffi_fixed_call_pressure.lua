local ffi = require("ffi")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
uint64_t sum7_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                  uint64_t e, uint64_t f, uint64_t g);
uint64_t sum5_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                  uint64_t e);
uint64_t sum6_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                  uint64_t e, uint64_t f);
double sum4_double(double a, double b, double c, double d);
double sum5_double(double a, double b, double c, double d, double e);
double sum6_double(double a, double b, double c, double d, double e, double f);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")

local function gpr_reg5_pressure(n)
  local total = u64(0)
  local i = 1
  while i <= n - 15 do
    local a0 = 16 * i + 120
    local a = a0
    local b = a0 + 16
    local c = a0 + 32
    local d = a0 + 48
    total = total + lib.sum5_u64(a, b, c, d, a)
    i = i + 16
  end
  while i <= n do
    local a = u64(i)
    local b = u64(i + 1)
    local c = u64(i + 2)
    local d = u64(i + 3)
    total = total + lib.sum5_u64(a, b, c, d, a)
    i = i + 1
  end
  return tonumber(total)
end

local function gpr_stack6_pressure(n)
  local total = u64(0)
  local i = 1
  while i <= n - 15 do
    local a0 = 16 * i + 120
    local a = a0
    local b = a0 + 16
    local c = a0 + 32
    local d = a0 + 48
    total = total + lib.sum6_u64(a, b, c, d, a, b)
    i = i + 16
  end
  while i <= n do
    local a = u64(i)
    local b = u64(i + 1)
    local c = u64(i + 2)
    local d = u64(i + 3)
    total = total + lib.sum6_u64(a, b, c, d, a, b)
    i = i + 1
  end
  return tonumber(total)
end

local function gpr_stack7_pressure(n)
  local total = u64(0)
  local i = 1
  while i <= n - 15 do
    local a0 = 16 * i + 120
    local a = a0
    local b = a0 + 16
    local c = a0 + 32
    local d = a0 + 48
    total = total + lib.sum7_u64(a, b, c, d, a, b, c)
    i = i + 16
  end
  while i <= n do
    local a = u64(i)
    local b = u64(i + 1)
    local c = u64(i + 2)
    local d = u64(i + 3)
    total = total + lib.sum7_u64(a, b, c, d, a, b, c)
    i = i + 1
  end
  return tonumber(total)
end

local function fpr_reg4_pressure(n)
  local total = 0
  local i = 1
  while i <= n - 15 do
    local a = 16 * i + 124
    local b = a + 20
    local c = a + 40
    total = total + lib.sum4_double(a, b, c, a)
    i = i + 16
  end
  while i <= n do
    local a = i + 0.25
    local b = i + 1.5
    local c = i + 2.75
    total = total + lib.sum4_double(a, b, c, a)
    i = i + 1
  end
  return total
end

local function fpr_stack5_pressure(n)
  local total = 0
  local i = 1
  while i <= n - 15 do
    local a = 16 * i + 124
    local b = a + 20
    local c = a + 40
    total = total + lib.sum5_double(a, b, c, a, b)
    i = i + 16
  end
  while i <= n do
    local a = i + 0.25
    local b = i + 1.5
    local c = i + 2.75
    total = total + lib.sum5_double(a, b, c, a, b)
    i = i + 1
  end
  return total
end

local function fpr_stack6_pressure(n)
  local total = 0
  local i = 1
  while i <= n - 15 do
    local a = 16 * i + 124
    local b = a + 20
    local c = a + 40
    total = total + lib.sum6_double(a, b, c, a, b, c)
    i = i + 16
  end
  while i <= n do
    local a = i + 0.25
    local b = i + 1.5
    local c = i + 2.75
    total = total + lib.sum6_double(a, b, c, a, b, c)
    i = i + 1
  end
  return total
end

local scales = {
  small = 1000,
  medium = 5000,
  hot = 20000,
  xhot = 200000,
}

local workloads = {
  { "gpr_reg5_pressure", gpr_reg5_pressure },
  { "gpr_stack6_pressure", gpr_stack6_pressure },
  { "gpr_pressure", gpr_stack7_pressure },
  { "fpr_reg4_pressure", fpr_reg4_pressure },
  { "fpr_stack5_pressure", fpr_stack5_pressure },
  { "fpr_pressure", fpr_stack6_pressure },
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
        if name:sub(1, 3) == "gpr" then
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
