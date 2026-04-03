local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local function run_const()
  return 486
end

local function run_lua_abs()
  local total = 0
  for i = 1, 80000 do
    local x = (i % 17) - 8
    if x < 0 then
      x = -x
    end
    total = total + x
  end
  return total
end

local function drive(fn)
  local out = 0
  for _ = 1, 4 do
    out = fn()
  end
  return out
end

local expected_const = drive(run_const)
local expected_lua_abs = drive(run_lua_abs)

bench.run_suite({
  family = "lower_frame_same_callsite",
  cases = {
    {
      workload = "const_same_callsite",
      scale = "hot",
      iterations = 1,
      warmup_runs = 2,
      run = function()
        return drive(run_const)
      end,
      validate = function(result)
        bench.eq(result, expected_const, "const_same_callsite/hot")
      end,
    },
    {
      workload = "lua_abs_same_callsite",
      scale = "hot",
      iterations = 1,
      warmup_runs = 2,
      run = function()
        return drive(run_lua_abs)
      end,
      validate = function(result)
        bench.eq(result, expected_lua_abs, "lua_abs_same_callsite/hot")
      end,
    },
  },
})
