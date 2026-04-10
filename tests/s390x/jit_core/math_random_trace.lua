local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local function run(n)
  local total = 0
  for _ = 1, n do
    total = total + math.random()
  end
  return total
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")
math.randomseed(0)

local capture = t.trace_capture()
local total = run(200)
capture.stop()

t.truthy(total > 0 and total < 200, "math.random total range")
t.truthy(t.find_trace_event(capture.events, "stop"), "math.random traced")
