local bit = require("bit")
local jit = require("jit")

assert(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=1", "hotexit=1")

local function mix_ref(i)
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

local function mix_hot(i)
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

local function ref_loop(first, last, chunks)
  local total = 0
  for _ = 1, chunks do
    for i = first, last do
      total = bit.tobit(total + mix_ref(i))
    end
  end
  return total
end

local function hot_loop(first, last, chunks)
  local total = 0
  for _ = 1, chunks do
    for i = first, last do
      total = bit.tobit(total + mix_hot(i))
    end
  end
  return total
end

local function literal_1_200(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = bit.tobit(total + mix_hot(i))
    end
  end
  return total
end

local function literal_0_200(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 0, 200 do
      total = bit.tobit(total + mix_hot(i))
    end
  end
  return total
end

local function literal_2_200(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 2, 200 do
      total = bit.tobit(total + mix_hot(i))
    end
  end
  return total
end

local function literal_1_199(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 199 do
      total = bit.tobit(total + mix_hot(i))
    end
  end
  return total
end

jit.off(mix_ref, true)
jit.off(ref_loop, true)

local function run_cases()
  local cases = {
    {1, 200, 20},
    {0, 1, 1},
    {1, 1, 1},
    {2, 200, 3},
    {185, 200, 5},
    {1, 199, 2},
    {1, 200, 1},
    {0, 200, 2},
    {55, 60, 7},
    {193, 200, 11},
  }
  local literal_cases = {
    {literal_1_200, 1, 200, 20},
    {literal_0_200, 0, 200, 2},
    {literal_2_200, 2, 200, 3},
    {literal_1_199, 1, 199, 2},
  }

  for round = 1, 25 do
    for i = 1, #cases do
      local c = cases[i]
      local got = hot_loop(c[1], c[2], c[3])
      local expected = ref_loop(c[1], c[2], c[3])
      if got ~= expected then
	error(("bitops suffix mixed stop r%d case%d: expected %d, got %d"):
	      format(round, i, expected, got), 0)
      end
    end
    for i = 1, #literal_cases do
      local c = literal_cases[i]
      local got = c[1](c[4])
      local expected = ref_loop(c[2], c[3], c[4])
      if got ~= expected then
	error(("bitops suffix literal r%d case%d: expected %d, got %d"):
	      format(round, i, expected, got), 0)
      end
    end
  end
end

jit.off(run_cases, true)
run_cases()
