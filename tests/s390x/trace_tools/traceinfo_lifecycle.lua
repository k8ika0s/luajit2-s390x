local jit = require("jit")
local util = require("jit.util")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")

local cap = t.trace_capture()
local total = 0

jit.opt.start("hotloop=2", "hotexit=2")

t.with_finally(function()
  cap.stop()
end, function()
  for i = 1, 200 do
    total = total + i
  end
end)

t.eq(total, 20100, "traceinfo total")

local stop_ev = t.find_trace_event(cap.events, "stop")
t.truthy(stop_ev, "trace stop event")
local traceno = tonumber(stop_ev[2])
t.truthy(traceno and traceno > 0, "trace number")

local info = util.traceinfo(traceno)
t.truthy(info, "traceinfo before flush")
t.truthy((tonumber(info.nins) or 0) > 0, "traceinfo nins")

jit.flush()
t.eq(util.traceinfo(traceno), nil, "traceinfo after flush")

print("traceinfo_lifecycle", traceno)
