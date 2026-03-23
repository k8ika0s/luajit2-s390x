local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")

local cap = t.trace_capture()
local total = 0

jit.opt.start("hotloop=2", "hotexit=2")

t.with_finally(function()
  cap.stop()
end, function()
  for i = 1, 120 do
    total = total + ((i * 3) % 17)
  end
end)

t.eq(total, 955, "root trace total")
t.truthy(t.find_trace_event(cap.events, "start"), "trace start event")
local stop_ev = t.assert_trace_stop(cap.events, "trace stop event")
local traceno = tonumber(stop_ev[2])
t.truthy(traceno and traceno > 0, "trace stop number")
t.truthy(#t.traceinfo_snapshot(traceno) >= 1, "traceinfo snapshot non-empty")

jit.flush()

print("trace_attach_root", #cap.events)
