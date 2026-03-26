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

local function assert_no_iterator_chain(limit, label)
  for tr = 1, limit do
    local info = jutil.traceinfo(tr)
    if not info then
      break
    end
    local link = tonumber(info.link) or 0
    local linktype = tostring(info.linktype or "")
    if tr > 2 and linktype == "root" and link == 2 then
      error(string.format("%s: unexpected root->2 trace at %d", label, tr), 2)
    end
    if tr > 2 and linktype == "loop" and link == tr then
      error(string.format("%s: unexpected loop->self trace at %d", label, tr), 2)
    end
  end
end

jit.off(interpreter_result, true)
jit.off(count_live_traces, true)
jit.off(assert_no_iterator_chain, true)

local expected = interpreter_result(run_pairs_shape, 2000, 4)

local first = run_pairs_shape(2000, 4)
t.eq(first, expected, "iterator trace shape first result")
local traces_after_first = count_live_traces(64)
t.truthy(traces_after_first > 0, "iterator trace shape traces first run")
if is_s390x then
  t.truthy(traces_after_first <= 4, "iterator trace shape bounded first run")
  assert_no_iterator_chain(64, "iterator trace shape first run")
end

local second = run_pairs_shape(2000, 4)
t.eq(second, expected, "iterator trace shape second result")
local traces_after_second = count_live_traces(64)
if is_s390x then
  t.eq(traces_after_second, traces_after_first, "iterator trace shape stable trace count")
  t.truthy(traces_after_second <= 4, "iterator trace shape bounded second run")
  assert_no_iterator_chain(64, "iterator trace shape second run")
end

print("iterator_trace_shape", traces_after_first, traces_after_second)
