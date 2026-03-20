local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

ffi.cdef[[
typedef struct {
  int32_t x;
  int32_t y;
} pair_t;
]]

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")
local capture = t.trace_capture()

local box = ffi.new("pair_t[1]")
for i = 1, 200 do
  box[0].x = i
  box[0].y = box[0].y + box[0].x
end

capture.stop()

t.eq(tonumber(box[0].y), 20100, "ffi cdata trace total")
t.truthy(t.find_trace_event(capture.events, "start"), "ffi cdata trace start")
t.truthy(t.find_trace_event(capture.events, "stop"), "ffi cdata trace stop")
