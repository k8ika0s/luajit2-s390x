local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")

local total = 0
for i = 1, 200 do
  local x = i + 0.5
  total = total + (x % 2.25)
end

t.approx(total, 199.75, 1e-12, "mod trace total")
