local ffi = require("ffi")
local jit = require("jit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

ffi.cdef[[
int abs(int x);
]]

local cabs = ffi.C.abs

local function direct_abs_literal_stop_real()
  local total = 0
  for i = 1, 80000 do
    total = total + ffi.C.abs((i % 17) - 8)
  end
  return total
end

local function stored_abs_literal_stop_real()
  local total = 0
  for i = 1, 80000 do
    total = total + cabs((i % 17) - 8)
  end
  return total
end

jit.off(direct_abs_literal_stop_real, true)
jit.off(stored_abs_literal_stop_real, true)
local expected_direct = direct_abs_literal_stop_real()
local expected_stored = stored_abs_literal_stop_real()
jit.on(direct_abs_literal_stop_real, true)
jit.on(stored_abs_literal_stop_real, true)

bench.run_suite({
  family = "ffi_calls_static_stop",
  cases = {
    {
      workload = "direct_abs_literal_stop_real",
      scale = "hot",
      iterations = 1,
      warmup_runs = 2,
      run = direct_abs_literal_stop_real,
      validate = function(result)
        bench.eq(result, expected_direct, "direct_abs_literal_stop_real/hot")
      end,
    },
    {
      workload = "stored_abs_literal_stop_real",
      scale = "hot",
      iterations = 1,
      warmup_runs = 2,
      run = stored_abs_literal_stop_real,
      validate = function(result)
        bench.eq(result, expected_stored, "stored_abs_literal_stop_real/hot")
      end,
    },
  },
})
