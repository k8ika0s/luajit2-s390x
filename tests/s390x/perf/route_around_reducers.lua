local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scale_order = { "hot" }
local scales = {
  hot = 400,
}

local function be_pack_literal_stop(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = bit.band(bit.rshift(i, 24), 0xff)
      local b2 = bit.band(bit.rshift(i, 16), 0xff)
      local b3 = bit.band(bit.rshift(i, 8), 0xff)
      local b4 = bit.band(i, 0xff)
      total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
    end
  end
  return bit.tobit(total)
end

local function be_pack_literal_stop_local_ops(chunks)
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = band(rshift(i, 24), 0xff)
      local b2 = band(rshift(i, 16), 0xff)
      local b3 = band(rshift(i, 8), 0xff)
      local b4 = band(i, 0xff)
      total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
    end
  end
  return tobit(total)
end

local function be_pack_loop_local_ops(chunks)
  local total = 0
  local band = bit.band
  local rshift = bit.rshift
  local lshift = bit.lshift
  local tobit = bit.tobit
  for _ = 1, chunks do
    for i = 1, 400 do
      local b1 = band(rshift(i, 24), 0xff)
      local b2 = band(rshift(i, 16), 0xff)
      local b3 = band(rshift(i, 8), 0xff)
      local b4 = band(i, 0xff)
      total = tobit(total + lshift(b1, 24) + lshift(b2, 16) + lshift(b3, 8) + b4)
    end
  end
  return tobit(total)
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local chunks = scales[scale]
  local expected_be_pack = be_pack_literal_stop(chunks)
  local expected_be_pack_local = be_pack_literal_stop_local_ops(chunks)
  local expected_be_pack_loop_local = be_pack_loop_local_ops(chunks)
  cases[#cases + 1] = {
    workload = "be_pack_literal_stop",
    scale = scale,
    iterations = chunks,
    warmup_runs = 2,
    run = be_pack_literal_stop,
    validate = function(result)
      bench.eq(result, expected_be_pack, "be_pack_literal_stop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "be_pack_literal_stop_local_ops",
    scale = scale,
    iterations = chunks,
    warmup_runs = 2,
    run = be_pack_literal_stop_local_ops,
    validate = function(result)
      bench.eq(result, expected_be_pack_local, "be_pack_literal_stop_local_ops/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "be_pack_loop_local_ops",
    scale = scale,
    iterations = chunks,
    warmup_runs = 2,
    run = be_pack_loop_local_ops,
    validate = function(result)
      bench.eq(result, expected_be_pack_loop_local, "be_pack_loop_local_ops/" .. scale)
    end,
  }
end

bench.run_suite({ family = "route_around_reducers_truth_pack", cases = cases })
