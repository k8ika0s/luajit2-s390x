local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=1", "hotexit=2")

local numbers = { 2, 4, 6 }

local function sum_ipairs(n)
  local total = 0
  for _ = 1, n do
    for _, value in ipairs(numbers) do
      total = total + value
    end
  end
  return total
end

jit.off(sum_ipairs, true)
local expected = sum_ipairs(16000)
jit.on(sum_ipairs, true)
jit.flush()

t.eq(sum_ipairs(16000), expected, "ipairs value differs from key")
