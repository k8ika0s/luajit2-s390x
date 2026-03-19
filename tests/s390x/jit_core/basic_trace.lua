local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local sum = 0
for i = 1, 200 do
  sum = sum + i
end

t.eq(sum, 20100, "basic trace sum")
