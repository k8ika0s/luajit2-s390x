local t = require("tests.s390x.helpers.testlib")

local function collect(...)
  return select("#", ...), { ... }
end

local count, values = collect("a", 2, true, nil, "tail")
t.eq(count, 5, "vararg count")
t.eq(values[1], "a", "vararg first")
t.eq(values[2], 2, "vararg second")
t.eq(values[3], true, "vararg third")
t.eq(values[5], "tail", "vararg tail")

local hook_hits = 0
debug.sethook(function()
  hook_hits = hook_hits + 1
end, "", 5)

local sum = 0
for i = 1, 200 do
  sum = sum + i
end
debug.sethook()

t.eq(sum, 20100, "hooked loop sum")
t.truthy(hook_hits > 0, "hook was hit")
