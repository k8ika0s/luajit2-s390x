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

local function chain_tail_store(chunks)
  local total = 0
  local sink = { 0 }
  for _ = 1, chunks do
    for i = 1, 200 do
      local x = chain(i)
      sink[1] = x
      local y = sink[1]
      if x == y then
        total = total + 1
      end
    end
  end
  return bit.tobit(total + sink[1])
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local chunks = scales[scale]
  local expected = chain_tail_store(chunks)
  cases[#cases + 1] = {
    workload = "chain_tail_store",
    scale = scale,
    iterations = chunks,
    run = chain_tail_store,
    validate = function(result)
      bench.eq(result, expected, "chain_tail_store/" .. scale)
    end,
  }
end

bench.run_suite({ family = "logical_chain_tail_store", cases = cases })
