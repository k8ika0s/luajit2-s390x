local bit = require("bit")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")

local total = 0
for i = 1, 200 do
  total = total + bit.tobit(i * 65537)
end

t.eq(total, 1317293700, "bit.tobit helper")
