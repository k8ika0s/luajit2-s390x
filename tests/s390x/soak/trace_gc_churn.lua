local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local function make_workers()
  local workers = {}
  for i = 1, 8 do
    workers[i] = function(x)
      return x + i
    end
  end
  return workers
end

local function run()
  local workers = make_workers()
  local keep = {}
  local total = 0
  for round = 1, 48 do
    for i = 1, 48 do
      local tag = "trace-gc-" .. ((round + i) % 19)
      local item = { tag = tag, value = round * i }
      keep[(round + i) % 64 + 1] = item
      total = total + workers[(i % #workers) + 1](item.value) + #item.tag
    end
    if round % 6 == 0 then
      collectgarbage("collect")
      jit.flush()
    end
  end
  return total
end

local expected = run()
t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")
local actual = run()
t.eq(actual, expected, "trace gc churn")
