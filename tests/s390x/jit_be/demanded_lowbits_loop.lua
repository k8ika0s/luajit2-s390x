local bit = require("bit")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local function masked_bswap_sum(first, last)
  local total = 0
  for i = first, last do
    local x = bit.bxor(bit.lshift(i, 3), bit.bswap(i))
    total = bit.tobit(total + bit.band(x, 0x3ff))
  end
  return total
end

local function masked_bswap_with_outside_use(n)
  local total = 0
  for i = 1, n do
    local y = bit.bswap(i)
    local masked = bit.band(bit.bxor(y, i), 0x3ff)
    total = bit.tobit(total + masked + bit.band(y, 0xffff))
  end
  return total
end

local function reference(fn, ...)
  jit.off(fn, true)
  local out = { fn(...) }
  jit.on(fn, true)
  return unpack(out)
end

local ref_small = reference(masked_bswap_sum, 1, 200)
local ref_boundary = reference(masked_bswap_sum, 1, 65535)
local ref_past_boundary = reference(masked_bswap_sum, 1, 65536)
local ref_negative = reference(masked_bswap_sum, -12, 200)
local ref_outside = reference(masked_bswap_with_outside_use, 400)

jit.flush()
local cap = t.trace_capture()
local got_small = masked_bswap_sum(1, 200)
local got_boundary = masked_bswap_sum(1, 65535)
local got_past_boundary = masked_bswap_sum(1, 65536)
local got_negative = masked_bswap_sum(-12, 200)
local got_outside = masked_bswap_with_outside_use(400)
cap.stop()

t.truthy(t.find_trace_event(cap.events, "stop"), "demanded lowbits traced")
t.eq(got_small, ref_small, "masked bswap small SCEV")
t.eq(got_boundary, ref_boundary, "masked bswap 65535 boundary")
t.eq(got_past_boundary, ref_past_boundary, "masked bswap past boundary")
t.eq(got_negative, ref_negative, "masked bswap negative start")
t.eq(got_outside, ref_outside, "masked bswap outside use")
