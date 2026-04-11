local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local values = { "1.25", "2.5", "3.75", "4.125" }

local function parse_sum(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(values[(i % #values) + 1])
  end
  return total
end

local function parse_exit(n)
  local total = 0
  for i = 1, n do
    local s = (i == n) and "not-a-number" or values[(i % #values) + 1]
    local x = tonumber(s)
    total = total + (x or 17)
  end
  return total
end

jit.off(parse_sum, true)
jit.off(parse_exit, true)
local expected_sum = parse_sum(10000)
local expected_exit = parse_exit(10000)
jit.on(parse_sum, true)
jit.on(parse_exit, true)
jit.flush()

t.approx(parse_sum(10000), expected_sum, 1e-9, "strto trace sum")
t.approx(parse_exit(10000), expected_exit, 1e-9, "strto trace exit")
