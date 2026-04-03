local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 4000,
  medium = 16000,
  hot = 64000,
}

local function number_helper_loop_local_tobit(n)
  local total = 0
  local tobit = bit.tobit
  for i = 1, n do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end

local function be_pack_loop_local_ops_real(n)
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for i = 1, n do
    local b1 = band(rshift(i, 24), 0xff)
    local b2 = band(rshift(i, 16), 0xff)
    local b3 = band(rshift(i, 8), 0xff)
    local b4 = band(i, 0xff)
    total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
  end
  return tobit(total)
end

local cases = {}
for scale, n in pairs(scales) do
  local expected_helper = number_helper_loop_local_tobit(n)
  local expected_pack = be_pack_loop_local_ops_real(n)
  cases[#cases + 1] = {
    workload = "number_helper_loop_local_tobit",
    scale = scale,
    iterations = n,
    run = number_helper_loop_local_tobit,
    validate = function(result)
      bench.eq(result, expected_helper, "number_helper_loop_local_tobit/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "be_pack_loop_local_ops_real",
    scale = scale,
    iterations = n,
    run = be_pack_loop_local_ops_real,
    validate = function(result)
      bench.eq(result, expected_pack, "be_pack_loop_local_ops_real/" .. scale)
    end,
  }
end

bench.run_suite({ family = "be_helpers_localized", cases = cases })
