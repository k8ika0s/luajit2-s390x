local ffi = require("ffi")
local bench = dofile("tests/s390x/perf/benchlib.lua")

ffi.cdef[[
int abs(int x);
]]

local scales = {
  small = 4000,
  medium = 20000,
  hot = 80000,
}

local cabs = ffi.C.abs

local function direct_abs(n)
  local total = 0
  for i = 1, n do
    total = total + ffi.C.abs((i % 17) - 8)
  end
  return total
end

local function stored_abs(n)
  local total = 0
  for i = 1, n do
    total = total + cabs((i % 17) - 8)
  end
  return total
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local expected_direct = direct_abs(n)
  local expected_stored = stored_abs(n)
  cases[#cases + 1] = {
    workload = "direct_abs",
    scale = scale,
    iterations = n,
    run = direct_abs,
    validate = function(result)
      bench.eq(result, expected_direct, "direct_abs/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "stored_abs",
    scale = scale,
    iterations = n,
    run = stored_abs,
    validate = function(result)
      bench.eq(result, expected_stored, "stored_abs/" .. scale)
    end,
  }
end

bench.run_suite({ family = "ffi_calls", cases = cases })
