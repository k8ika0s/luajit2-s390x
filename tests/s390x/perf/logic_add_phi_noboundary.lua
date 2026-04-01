local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scale_order = { "small", "medium", "hot" }
local scales = {
  small = 1,
  medium = 5,
  hot = 20,
}

local function chain(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return bit.band(x, 0x3ff)
end

local function logic_add_phi_noboundary(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = total + chain(i)
    end
  end
  return total
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local chunks = scales[scale]
  local expected = logic_add_phi_noboundary(chunks)
  cases[#cases + 1] = {
    workload = "logic_add_phi_noboundary",
    scale = scale,
    iterations = chunks,
    run = logic_add_phi_noboundary,
    validate = function(result)
      bench.eq(result, expected, "logic_add_phi_noboundary/" .. scale)
    end,
  }
end

bench.run_suite({ family = "logic_add_phi_noboundary", cases = cases })
