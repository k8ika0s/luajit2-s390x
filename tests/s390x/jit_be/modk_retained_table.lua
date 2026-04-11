local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local function run_retained_mod_table(rounds, inner)
  local keep = {}
  local total = 0
  for round = 1, rounds do
    for i = 1, inner do
      local tag = "trace-gc-" .. ((round + i) % 19)
      local item = { tag = tag, value = round * i }
      keep[((round + i) % 64) + 1] = item
      total = total + #tag
    end
  end
  return total
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")

t.eq(run_retained_mod_table(48, 48), 24145, "mod retained table string length")
