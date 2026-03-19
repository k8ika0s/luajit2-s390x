local t = require("tests.s390x.helpers.testlib")

local tab = {}
for i = 1, 20 do
  tab["k" .. i] = i
end

local total = 0
for _, value in pairs(tab) do
  total = total + value
end
t.eq(total, 210, "pairs sum")

local seen = {}
local key, value = next(tab, nil)
while key do
  seen[key] = value
  key, value = next(tab, key)
end
t.eq(seen.k7, 7, "next traversal")

local function gen(limit)
  local current = 0
  return function()
    current = current + 1
    if current > limit then
      return nil
    end
    return current, current * 3
  end
end

local accum = 0
for _, v in gen(10) do
  accum = accum + v
end
t.eq(accum, 165, "custom iterator")
