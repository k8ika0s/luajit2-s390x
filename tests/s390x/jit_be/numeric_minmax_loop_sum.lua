local t = require("tests.s390x.helpers.testlib")
local jit = require("jit")

jit.opt.start("hotloop=1", "hotexit=2")

local function min_expected(n)
  local m = math.floor(n / 2)
  if n % 2 == 0 then
    return m * (m + 1)
  end
  return (m + 1) * (m + 1)
end

local function max_expected(n)
  return n * (n + 1) - min_expected(n)
end

local function load_loop(src, name)
  return assert(loadstring(src, name))()
end

local min_loop = load_loop([[
return function(n)
  local total = 0
  for i = 1, n do
    total = total + math.min(i, n + 1 - i)
  end
  return total
end
]], "@numeric_ops_min")

local max_loop = load_loop([[
return function(n)
  local total = 0
  for i = 1, n do
    total = total + math.max(i, n + 1 - i)
  end
  return total
end
]], "@numeric_ops_max")

local numeric_minmax_loop = load_loop([[
return function(n)
  local total = 0.5
  for i = 1, n do
    total = total + math.min(i + 0.25, n + 0.75 - i)
    total = total + math.max(i + 0.125, n + 0.5 - i)
  end
  return total
end
]], "@numeric_ops_num_minmax")

local numeric_minmax_ref = load_loop([[
return function(n)
  local total = 0.5
  for i = 1, n do
    total = total + math.min(i + 0.25, n + 0.75 - i)
    total = total + math.max(i + 0.125, n + 0.5 - i)
  end
  return total
end
]], "@numeric_ops_num_minmax_ref")

jit.off(numeric_minmax_ref, true)

for _, n in ipairs({1, 2, 3, 4, 127, 128, 64000, 70000}) do
  t.eq(min_loop(n), min_expected(n), "min_loop(" .. n .. ")")
  t.eq(max_loop(n), max_expected(n), "max_loop(" .. n .. ")")
end

jit.flush()
local cap = t.trace_counter_capture_lite()
for _, n in ipairs({1, 2, 3, 4, 127, 128, 200}) do
  local got = numeric_minmax_loop(n)
  local expected = numeric_minmax_ref(n)
  t.approx(got, expected, 1e-9, "numeric_minmax_loop(" .. n .. ")")
end
cap.stop()
t.truthy(cap.stop_count > 0, "numeric min/max traced")
