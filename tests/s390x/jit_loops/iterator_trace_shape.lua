local jit = require("jit")
local jutil = require("jit.util")
local t = require("tests.s390x.helpers.testlib")
local is_s390x = jit.arch == "s390x"

if not is_s390x then
  print("iterator_trace_shape skip non-s390x")
  return
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=1", "minstitch=1")

local base_array = { 1, 3, 5, 7, 9 }

local function run_pairs_shape(n, outer)
  local total = 0
  for _ = 1, outer do
    for _ = 1, n do
      for _, value in pairs(base_array) do
        total = total + value
      end
    end
  end
  return total
end

local function interpreter_result(fn, ...)
  jit.off(fn, true)
  local result = fn(...)
  jit.on(fn, true)
  jit.flush()
  return result
end

local function count_live_traces(limit)
  local count = 0
  for tr = 1, limit or 64 do
    if not jutil.traceinfo(tr) then
      break
    end
    count = count + 1
  end
  return count
end

jit.off(interpreter_result, true)
jit.off(count_live_traces, true)

local expected = interpreter_result(run_pairs_shape, 2000, 4)

local first = run_pairs_shape(2000, 4)
t.eq(first, expected, "iterator trace correctness first result")
local traces_after_first = count_live_traces(64)
t.truthy(traces_after_first > 0, "iterator trace correctness traces first run")

local second = run_pairs_shape(2000, 4)
t.eq(second, expected, "iterator trace correctness second result")
local traces_after_second = count_live_traces(64)
t.truthy(traces_after_second > 0, "iterator trace correctness traces second run")

print("iterator_trace_correctness", traces_after_first, traces_after_second)
