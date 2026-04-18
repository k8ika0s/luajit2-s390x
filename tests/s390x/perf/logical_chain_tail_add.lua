local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scale_order = { "hot", "xhot" }
local scales = {
  hot = 20, xhot = 2000,
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
  return x
end

local function chain_tail_add(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = bit.tobit(total + chain(i))
    end
  end
  return total
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local chunks = scales[scale]
  local expected = chain_tail_add(chunks)
  cases[#cases + 1] = {
    workload = "chain_tail_add",
    scale = scale,
    iterations = chunks,
    run = chain_tail_add,
    validate = function(result)
      bench.eq(result, expected, "chain_tail_add/" .. scale)
    end,
  }
end

bench.run_suite({ family = "logical_chain_tail_add", cases = cases })
