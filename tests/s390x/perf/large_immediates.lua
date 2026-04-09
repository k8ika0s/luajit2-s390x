local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 4000,
  medium = 16000,
  hot = 40000,
}

local arr = {}
arr[4] = 19
arr[5000] = 73

local function add_small(n)
  local total = 0
  for _ = 1, n do
    total = total + 7
  end
  return total
end

local function add_large(n)
  local total = 0
  for _ = 1, n do
    total = total + 40000
  end
  return total
end

local function aref_small(n)
  local total = 0
  for _ = 1, n do
    total = total + arr[4]
  end
  return total
end

local function aref_large(n)
  local total = 0
  for _ = 1, n do
    total = total + arr[5000]
  end
  return total
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local add_small_expected = add_small(n)
  local add_large_expected = add_large(n)
  local aref_small_expected = aref_small(n)
  local aref_large_expected = aref_large(n)
  cases[#cases + 1] = {
    workload = "add_small",
    scale = scale,
    iterations = n,
    run = add_small,
    validate = function(result)
      bench.eq(result, add_small_expected, "add_small/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "add_large",
    scale = scale,
    iterations = n,
    run = add_large,
    validate = function(result)
      bench.eq(result, add_large_expected, "add_large/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "aref_small",
    scale = scale,
    iterations = n,
    run = aref_small,
    validate = function(result)
      bench.eq(result, aref_small_expected, "aref_small/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "aref_large",
    scale = scale,
    iterations = n,
    run = aref_large,
    validate = function(result)
      bench.eq(result, aref_large_expected, "aref_large/" .. scale)
    end,
  }
end

bench.run_suite({ family = "large_immediates", cases = cases })
