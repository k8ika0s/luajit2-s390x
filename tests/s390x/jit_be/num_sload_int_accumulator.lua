local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2", "minstitch=1")

local function sum_fractional(n)
  local total = 0
  for i = 1, n do
    total = total + (i + 0.25)
  end
  return total
end

local function expected_sum(n)
  return (n * (n + 1)) / 2 + n * 0.25
end

local function run_three()
  local n = 20000
  return sum_fractional(n), sum_fractional(n), sum_fractional(n)
end

jit.flush()
local cap = t.trace_capture()
local a, b, c = t.with_finally(function()
  cap.stop()
end, run_three)

t.truthy(t.find_trace_event(cap.events, "stop"), "fractional accumulator traced")
t.eq(a, expected_sum(20000), "first fractional sum")
t.eq(b, expected_sum(20000), "second fractional sum")
t.eq(c, expected_sum(20000), "third fractional sum")
