local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=1")

local total = 0
for i = 1, 200 do
  local value = i
  if i % 7 == 0 then
    value = value * 3
  end
  total = total + value
end

t.eq(total, 25784, "side exit total")
