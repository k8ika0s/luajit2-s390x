local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 4000,
  medium = 16000,
  hot = 64000,
}

local function number_helper_loop(n)
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end

local function be_pack_loop(n)
  local total = 0
  for i = 1, n do
    local b1 = bit.band(bit.rshift(i, 24), 0xff)
    local b2 = bit.band(bit.rshift(i, 16), 0xff)
    local b3 = bit.band(bit.rshift(i, 8), 0xff)
    local b4 = bit.band(i, 0xff)
    total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
  end
  return bit.tobit(total)
end

local cases = {}
for scale, n in pairs(scales) do
  local expected_helper = number_helper_loop(n)
  local expected_pack = be_pack_loop(n)
  cases[#cases + 1] = {
    workload = "number_helper_loop",
    scale = scale,
    iterations = n,
    run = number_helper_loop,
    validate = function(result)
      bench.eq(result, expected_helper, "number_helper_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "be_pack_loop",
    scale = scale,
    iterations = n,
    run = be_pack_loop,
    validate = function(result)
      bench.eq(result, expected_pack, "be_pack_loop/" .. scale)
    end,
  }
end

bench.run_suite({ family = "be_helpers", cases = cases })
