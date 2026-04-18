local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=1", "hotexit=2")

local function div_sqrt_loop(n)
  local total = 0
  for i = 1, n do
    total = total + ((i + 0.5) / (i + 1.25)) + math.sqrt(i + 0.25)
  end
  return total
end

local function ref(n)
  jit.off(div_sqrt_loop, true)
  local result = div_sqrt_loop(n)
  jit.on(div_sqrt_loop, true)
  jit.flush()
  return result
end

local expected = ref(64000)

for warmups = 0, 4 do
  jit.flush()
  for _ = 1, warmups do
    div_sqrt_loop(20)
  end
  t.approx(div_sqrt_loop(64000), expected, 1e-9,
	   "div/sqrt combined loop warmups=" .. warmups)
end
