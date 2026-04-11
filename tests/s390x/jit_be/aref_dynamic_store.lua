local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local function run_dynamic_store(rounds, inner)
  local keep = {}
  local total = 0
  for round = 1, rounds do
    for i = 1, inner do
      local item = { value = round * i }
      keep[i] = item
      total = total + item.value
    end
  end
  return total
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")

t.eq(run_dynamic_store(48, 48), 1382976, "dynamic AREF store value total")
