local bit = require("bit")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")
local capture = t.trace_capture()

local function mix(i)
  local x = bit.band(i, 0xff)
  x = bit.bxor(x, bit.lshift(i, 3))
  x = bit.bor(x, bit.rshift(i, 1))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 5))
  x = bit.bxor(x, bit.ror(i, 7))
  x = bit.bxor(x, bit.bswap(i))
  x = bit.bxor(x, bit.bnot(i))
  return x
end

local total = 0
for i = 1, 200 do
  total = bit.tobit(total + mix(i))
end

capture.stop()

t.eq(total, 873075307, "bitops trace total")
t.truthy(t.find_trace_event(capture.events, "start"), "bitops trace start")
t.truthy(t.find_trace_event(capture.events, "stop"), "bitops trace stop")
