local bit = require("bit")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=1", "hotexit=2")

local function localized_tobit_mul_loop(n)
  local total = 0
  local tobit = bit.tobit
  for i = 1, n do
    total = tobit(total + i * 65537)
  end
  return tobit(total)
end

local function global_tobit_mul_loop(n)
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end

local function trace_result(fn, n)
  jit.flush()
  local cap = t.trace_capture()
  local result = t.with_finally(function()
    cap.stop()
  end, function()
    fn(20); fn(20); fn(20)
    return fn(n)
  end)
  t.truthy(t.find_trace_event(cap.events, "stop"), "loop traced")
  return result
end

jit.off(localized_tobit_mul_loop, true)
local expected_localized = localized_tobit_mul_loop(64000)
jit.on(localized_tobit_mul_loop, true)
t.eq(expected_localized, -149783296, "localized interpreter reference")
t.eq(trace_result(localized_tobit_mul_loop, 32767), -536887296,
     "localized before overflow")
t.eq(trace_result(localized_tobit_mul_loop, 32768), 1610629120,
     "localized first overflow exit")
t.eq(trace_result(localized_tobit_mul_loop, 64000), expected_localized,
     "localized overflow exits")

jit.off(global_tobit_mul_loop, true)
local expected_global = global_tobit_mul_loop(64000)
jit.on(global_tobit_mul_loop, true)
t.eq(expected_global, -149783296, "global interpreter reference")
t.eq(trace_result(global_tobit_mul_loop, 64000), expected_global,
     "global overflow exits")
