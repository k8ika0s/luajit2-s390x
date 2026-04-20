local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local upvalue_sink = 0

local function store_numbers(n)
  local out = {}
  local hash = {}
  local total = 0
  for i = 1, n do
    local v = i + 0.5
    out[i] = v
    hash.fixed = v + 1
    upvalue_sink = v + 2
    total = total + out[i] + hash.fixed + upvalue_sink
  end
  return total, out[n], hash.fixed, upvalue_sink
end

local function reference(n)
  upvalue_sink = 0
  jit.off(store_numbers, true)
  local total, last_array, last_hash, last_upvalue = store_numbers(n)
  jit.on(store_numbers, true)
  return total, last_array, last_hash, last_upvalue
end

local expected_total, expected_array, expected_hash, expected_upvalue = reference(256)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")
jit.flush()

local cap = t.trace_capture()
upvalue_sink = 0
local total, last_array, last_hash, last_upvalue = store_numbers(256)
cap.stop()

t.truthy(t.find_trace_event(cap.events, "stop"), "numeric AHU store traced")
t.approx(total, expected_total, 1e-12, "numeric AHU store total")
t.approx(last_array, expected_array, 1e-12, "numeric ASTORE last")
t.approx(last_hash, expected_hash, 1e-12, "numeric HSTORE last")
t.approx(last_upvalue, expected_upvalue, 1e-12, "numeric USTORE last")
