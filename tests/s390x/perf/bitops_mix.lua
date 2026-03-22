local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scale_order = { "small", "medium", "hot" }
local scales = {
  small = 1,
  medium = 5,
  hot = 20,
}

local function mix(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end

local function mix_bits(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = bit.tobit(total + mix(i))
    end
  end
  return total
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local chunks = scales[scale]
  local expected_mix = mix_bits(chunks)
  cases[#cases + 1] = {
    workload = "mix_bits",
    scale = scale,
    iterations = chunks,
    run = mix_bits,
    validate = function(result)
      bench.eq(result, expected_mix, "mix_bits/" .. scale)
    end,
  }
end

bench.run_suite({ family = "bitops_mix", cases = cases })
