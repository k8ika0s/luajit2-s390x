local t = require("tests.s390x.helpers.testlib")

local tab = {}
for i = 1, 64 do
  tab[i] = i * 2
end
tab.alpha = "a"
tab.beta = "b"

local sum = 0
for i = 1, 64 do
  sum = sum + tab[i]
end
t.eq(sum, 4160, "array sum")
t.eq(tab.alpha .. tab.beta, "ab", "hash fields")

local clone = {}
for k, v in pairs(tab) do
  clone[k] = v
end
t.eq(clone[32], 64, "clone array slot")
t.eq(clone.beta, "b", "clone hash slot")

local keys = {}
for k in pairs(tab) do
  keys[#keys + 1] = k
end
t.eq(#keys, 66, "pair count")
