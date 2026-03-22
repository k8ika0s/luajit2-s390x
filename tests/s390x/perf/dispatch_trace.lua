local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 4000,
  medium = 20000,
  hot = 80000,
}

local function numeric_loop(n)
  local total = 0
  for i = 1, n do
    total = total + (i % 97)
  end
  return total
end

local function side_exit_loop(n)
  local total = 0
  for i = 1, n do
    if i % 7 == 0 then
      total = total - (i % 97)
    else
      total = total + (i % 97)
    end
  end
  return total
end

local function hotexit_loop(n)
  local total = 0
  for i = 1, n do
    if i % 5 == 0 then
      total = total + ((i % 97) * 3)
    elseif i % 3 == 0 then
      total = total - (i % 97)
    else
      total = total + 1
    end
  end
  return total
end

local cases = {}
for scale, n in pairs(scales) do
  local numeric_expected = numeric_loop(n)
  local side_exit_expected = side_exit_loop(n)
  local hotexit_expected = hotexit_loop(n)
  cases[#cases + 1] = {
    workload = "numeric_loop",
    scale = scale,
    iterations = n,
    run = numeric_loop,
    validate = function(result)
      bench.eq(result, numeric_expected, "numeric_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "side_exit_loop",
    scale = scale,
    iterations = n,
    run = side_exit_loop,
    validate = function(result)
      bench.eq(result, side_exit_expected, "side_exit_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "hotexit_loop",
    scale = scale,
    iterations = n,
    run = hotexit_loop,
    validate = function(result)
      bench.eq(result, hotexit_expected, "hotexit_loop/" .. scale)
    end,
  }
end

bench.run_suite({ family = "dispatch_trace", cases = cases })
