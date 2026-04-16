local bit = require("bit")
local jit = require("jit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scale_order = { "small", "medium", "hot" }
local scales = {
  small = 1000,
  medium = 4000,
  hot = 16000,
}

local function sum(...)
  local total = 0
  for i = 1, select("#", ...) do
    total = total + select(i, ...)
  end
  return total
end

local function retlast(...)
  return select(select("#", ...), ...)
end

local function sum_loop(n)
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + sum(1, 2, 3, ((i - 1) % 17) + 1))
  end
  return result
end

local function retlast_loop(n)
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + retlast(1, 2, 3, ((i - 1) % 17) + 1))
  end
  return result
end

local function retconst(...)
  return 42
end

local function retconst_loop(n)
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + retconst(1, 2, 3, i))
  end
  return result
end

local function joff_expected(fn, n)
  local enabled = select(1, jit.status())
  if enabled then
    jit.off()
  end
  local ok, result = pcall(fn, n)
  if enabled then
    jit.on()
    jit.flush()
  end
  if not ok then
    error(result, 0)
  end
  return result
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local n = scales[scale]
  local expected_sum = joff_expected(sum_loop, n)
  local expected_last = joff_expected(retlast_loop, n)
  local expected_const = joff_expected(retconst_loop, n)
  cases[#cases + 1] = {
    workload = "sum_loop",
    scale = scale,
    iterations = n,
    run = sum_loop,
    validate = function(result)
      bench.eq(result, expected_sum, "sum_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "retlast_loop",
    scale = scale,
    iterations = n,
    run = retlast_loop,
    validate = function(result)
      bench.eq(result, expected_last, "retlast_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "retconst_loop",
    scale = scale,
    iterations = n,
    run = retconst_loop,
    validate = function(result)
      bench.eq(result, expected_const, "retconst_loop/" .. scale)
    end,
  }
end

bench.run_suite({ family = "vararg_paths", cases = cases })
