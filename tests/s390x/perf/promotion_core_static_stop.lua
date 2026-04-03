local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local function number_helper_literal_stop_real()
  local total = 0
  for i = 1, 64000 do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end

local function number_helper_literal_stop_real_local_tobit()
  local total = 0
  local tobit = bit.tobit
  for i = 1, 64000 do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end

local function be_pack_literal_stop_real()
  local total = 0
  for i = 1, 64000 do
    local b1 = bit.band(bit.rshift(i, 24), 0xff)
    local b2 = bit.band(bit.rshift(i, 16), 0xff)
    local b3 = bit.band(bit.rshift(i, 8), 0xff)
    local b4 = bit.band(i, 0xff)
    total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
  end
  return bit.tobit(total)
end

local cases = {
  {
    workload = "number_helper_literal_stop_real",
    scale = "hot",
    iterations = 1,
    warmup_runs = 2,
    run = number_helper_literal_stop_real,
    validate = function(result)
      bench.eq(result, number_helper_literal_stop_real(), "number_helper_literal_stop_real/hot")
    end,
  },
  {
    workload = "number_helper_literal_stop_real_local_tobit",
    scale = "hot",
    iterations = 1,
    warmup_runs = 2,
    run = number_helper_literal_stop_real_local_tobit,
    validate = function(result)
      bench.eq(result, number_helper_literal_stop_real_local_tobit(), "number_helper_literal_stop_real_local_tobit/hot")
    end,
  },
  {
    workload = "be_pack_literal_stop_real",
    scale = "hot",
    iterations = 1,
    warmup_runs = 2,
    run = be_pack_literal_stop_real,
    validate = function(result)
      bench.eq(result, be_pack_literal_stop_real(), "be_pack_literal_stop_real/hot")
    end,
  },
}

bench.run_suite({ family = "promotion_core_static_stop", cases = cases })
