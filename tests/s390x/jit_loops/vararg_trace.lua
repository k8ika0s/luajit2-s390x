local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")

local function sum(...)
  local total = 0
  for i = 1, select("#", ...) do
    total = total + select(i, ...)
  end
  return total
end

local result = 0
for i = 1, 100 do
  result = result + sum(1, 2, 3, i)
end

t.eq(result, 5650, "vararg trace")
