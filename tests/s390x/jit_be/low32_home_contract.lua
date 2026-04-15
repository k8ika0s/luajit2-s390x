local bit = require("bit")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local function callee(v)
  if v < 0 then
    return bit.band(bit.bnot(v), 0xff)
  end
  return bit.band(v, 0xff)
end

local function w32_value(i, carry)
  local x = bit.band(i * 17, 0x7fffffff)
  x = bit.bxor(x, bit.lshift(i, 5))
  x = bit.bxor(x, bit.rshift(i, 3))
  x = bit.bxor(x, bit.arshift(-i, 2))
  x = bit.bxor(x, bit.rol(i, 7))
  x = bit.bxor(x, bit.ror(carry, 11))
  x = bit.bxor(x, bit.bswap(i))
  return bit.tobit(x + carry)
end

local function contract_loop(n)
  local carry = 0
  local compare_total = 0
  local call_total = 0
  local store_total = 0
  local exit_total = 0
  local generic_total = 0
  local sink = {}
  for i = 1, n do
    local x = w32_value(i, carry)
    carry = x

    if x < 0 then
      compare_total = compare_total + 3
    else
      compare_total = compare_total + 7
    end

    call_total = bit.tobit(call_total + callee(x))

    local slot = bit.band(i, 7) + 1
    sink[slot] = x
    store_total = bit.tobit(store_total + sink[slot])

    if bit.band(i, 3) == 0 then
      exit_total = bit.tobit(exit_total + (x >= 0 and 11 or -13))
    else
      exit_total = bit.tobit(exit_total + (x == sink[slot] and 17 or -19))
    end

    generic_total = generic_total + (x / 65536)
  end
  return carry, compare_total, call_total, store_total, exit_total,
         math.floor(generic_total * 1000)
end

local function reference(n)
  jit.off(contract_loop, true)
  jit.off(callee, true)
  local a, b, c, d, e, f = contract_loop(n)
  jit.on(contract_loop, true)
  jit.on(callee, true)
  return a, b, c, d, e, f
end

local ea, eb, ec, ed, ee, ef = reference(400)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

jit.flush()
local cap = t.trace_capture()
local a, b, c, d, e, f = contract_loop(400)
cap.stop()

t.truthy(t.find_trace_event(cap.events, "stop"), "low32 contract traced")
t.eq(a, ea, "low32 final carry")
t.eq(b, eb, "low32 compare boundary")
t.eq(c, ec, "low32 call argument boundary")
t.eq(d, ed, "low32 store boundary")
t.eq(e, ee, "low32 snapshot exit boundary")
t.eq(f, ef, "low32 generic numeric boundary")
