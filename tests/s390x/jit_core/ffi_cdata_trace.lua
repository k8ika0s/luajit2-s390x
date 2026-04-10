local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

ffi.cdef[[
typedef struct {
  int32_t x;
  int32_t y;
} pair_t;

typedef struct {
  uint16_t a;
  uint32_t b;
  uint8_t c;
} packed_u_t;
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

local function mixed_width_loop(n)
  local slot = ffi.new("packed_u_t[1]")
  local total = 0
  for i = 1, n do
    slot[0].a = i % 65535
    slot[0].b = (i % 4096) * 17
    slot[0].c = i % 251
    total = total + slot[0].a + slot[0].b + slot[0].c
  end
  return total
end

jit.off(mixed_width_loop, true)
local expected_width = mixed_width_loop(200)
jit.on(mixed_width_loop, true)
jit.flush()
jit.opt.start("hotloop=2")

local narrow_xstore_enabled = os.getenv("LUAJIT_S390X_NARROW_XSTORE") ~= nil
local actual_width
if narrow_xstore_enabled then
  capture = t.trace_capture()
  actual_width = mixed_width_loop(200)
  capture.stop()
else
  actual_width = mixed_width_loop(200)
end

t.eq(actual_width, expected_width, "ffi mixed-width cdata trace total")
if narrow_xstore_enabled then
  t.truthy(t.find_trace_event(capture.events, "stop"),
           "ffi mixed-width cdata trace stop")
  t.eq(t.count_trace_events(capture.events, "abort"), 0,
       "ffi mixed-width cdata trace aborts")
end
