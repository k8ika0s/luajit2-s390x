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

local array = { 10, 20, 30 }
local array_total = 0
for _ = 1, 1000 do
  local key, value = next(array, nil)
  while key do
    array_total = array_total + key + value
    key, value = next(array, key)
  end
end

t.eq(array_total, 66000, "explicit next array successor total")
