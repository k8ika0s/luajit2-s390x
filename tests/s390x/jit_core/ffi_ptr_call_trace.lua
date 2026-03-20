local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

ffi.cdef("int abs(int x);")

local cabs = ffi.C.abs
local capture = t.trace_capture()
local total = 0

for i = 1, 200 do
  total = total + cabs((i % 11) - 5)
end

capture.stop()

t.eq(total, 547, "ffi ptr trace total")
t.truthy(t.find_trace_event(capture.events, "start"), "ffi ptr trace start")
t.truthy(t.find_trace_event(capture.events, "stop"), "ffi ptr trace stop")
