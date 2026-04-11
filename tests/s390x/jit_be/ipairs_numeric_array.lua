local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local numbers = { 1, 2, 3, 4, 5, 6, 7, 8 }

local function run_ipairs(n)
  local total = 0
  for i = 1, n do
    for _, value in ipairs(numbers) do
      total = total + value
    end
  end
  return total
end

local function run_indexed(n)
  local total = 0
  for i = 1, n do
    for j = 1, #numbers do
      total = total + numbers[j]
    end
  end
  return total
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

t.eq(run_indexed(16000), 576000, "indexed numeric array sum")
t.eq(run_ipairs(16000), 576000, "ipairs numeric array sum")
