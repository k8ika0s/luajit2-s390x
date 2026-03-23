local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")

local trace_cap = t.trace_capture()
local texit_cap = t.texit_capture()
local total = 0

jit.opt.start("hotloop=2", "hotexit=2", "minstitch=1")

t.with_finally(function()
  texit_cap.stop()
  trace_cap.stop()
  jit.flush()
end, function()
  for i = 1, 160 do
    local x = (i % 9 == 0) and (i * 2) or (i - 3)
    if x > 60 then
      total = total - 5
    else
      total = total + x
    end
  end
end)

t.eq(total, 1199, "texit total")
t.assert_trace_stop(trace_cap.events, "trace stop present")
t.truthy(#texit_cap.events >= 1, "texit events present")

print("texit_observer", #trace_cap.events, #texit_cap.events)
