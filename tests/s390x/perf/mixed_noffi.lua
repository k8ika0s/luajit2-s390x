local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 1000,
  medium = 4000,
  hot = 16000,
}

local numbers = { 1, 2, 3, 4, 5, 6, 7, 8 }

local function mixed_loop(n)
  local total = 0
  local map = { a = 1, b = 2, c = 3, d = 4 }
  for i = 1, n do
    total = total + bit.band(i * 17, 0x3ff)
    total = total + select(((i - 1) % 4) + 1, 1, 2, 3, 4)
    for _, value in ipairs(numbers) do
      total = total + value
    end
    for _, value in pairs(map) do
      total = total + value
    end
  end
  return total
end

local cases = {}
for scale, n in pairs(scales) do
  local expected = mixed_loop(n)
  cases[#cases + 1] = {
    workload = "mixed_loop",
    scale = scale,
    iterations = n,
    run = mixed_loop,
    validate = function(result)
      bench.eq(result, expected, "mixed_loop/" .. scale)
    end,
  }
end

bench.run_suite({ family = "mixed_noffi", cases = cases })
