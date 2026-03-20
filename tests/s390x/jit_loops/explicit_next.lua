local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2", "minstitch=1")

local tab = {}
for i = 1, 100 do
  tab["a" .. i] = i
end

local function f(tb, k)
  if not next(tb) then
    return nil
  end
  return k, tb["a" .. k]
end

local total = 0
for i = 1, 100 do
  local _, value = f(tab, i)
  if not value then
    break
  end
  total = total + value
end

t.eq(total, 5050, "explicit next total")
