local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2", "minstitch=1")

local tab = {}
for i = 1, 100 do
  tab["a" .. i] = i
end

local total = 0
for key in pairs(tab) do
  total = total + tab[key]
end

t.eq(total, 5050, "pairs total")
print("pairs total", total)
