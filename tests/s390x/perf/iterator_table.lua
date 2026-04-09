local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 4000,
  medium = 20000,
  hot = 80000,
}
local scale_order = bench.scale_order(scales)

local base_table = { a = 1, b = 2, c = 3, d = 4, e = 5 }
local base_array = { 1, 3, 5, 7, 9 }
local function pairs_sum(n)
  local total = 0
  for _ = 1, n do
    for _, value in pairs(base_table) do
      total = total + value
    end
  end
  return total
end

local function pairs_array_sum(n)
  local total = 0
  for _ = 1, n do
    for _, value in pairs(base_array) do
      total = total + value
    end
  end
  return total
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local n = scales[scale]
  local expected_pairs = pairs_sum(n)
  local expected_array_pairs = pairs_array_sum(n)
  cases[#cases + 1] = {
    workload = "pairs_sum",
    scale = scale,
    iterations = n,
    warmup_runs = 2,
    run = pairs_sum,
    validate = function(result)
      bench.eq(result, expected_pairs, "pairs_sum/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "pairs_array_sum",
    scale = scale,
    iterations = n,
    warmup_runs = 2,
    run = pairs_array_sum,
    validate = function(result)
      bench.eq(result, expected_array_pairs, "pairs_array_sum/" .. scale)
    end,
  }
end

bench.run_suite({ family = "iterator_table", cases = cases })
