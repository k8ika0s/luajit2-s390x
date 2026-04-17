local jit = require("jit")

jit.opt.start("hotloop=2", "hotexit=2")

local function abs_parity_sum(n)
  local total = 0
  for i = 1, n do
    local signed = (i % 2 == 0) and -i or i
    total = total + math.abs(signed)
  end
  return total
end

local function expect(n)
  return n * (n + 1) / 2
end

for _, n in ipairs({1, 2, 3, 4000, 16000, 64000, 65535, 65536, 70000}) do
  jit.flush()
  local got = abs_parity_sum(n)
  local exp = expect(n)
  assert(got == exp,
	 string.format("abs_parity_sum(%d): expected %.17g, got %.17g",
		       n, exp, got))
end

local old_abs = math.abs
math.abs = function(_)
  return 7
end
jit.flush()
local got = abs_parity_sum(1000)
math.abs = old_abs
assert(got == 7000, "rebound math.abs expected 7000, got " .. tostring(got))

print("abs_parity_loop_sum PASS")
