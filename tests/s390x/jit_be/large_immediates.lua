local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local function expect_trace(label, fn)
  jit.flush()
  local cap = t.trace_capture()
  local ok, a, b, c = t.with_finally(function()
    cap.stop()
  end, fn)
  t.truthy(t.find_trace_event(cap.events, "stop"), label .. " traced")
  return ok, a, b, c
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")

local function run_large_add(n)
  local total = 0
  for _ = 1, n do
    total = total + 40000
  end
  return total
end

local function run_large_sub(n)
  local total = n * 40000
  for _ = 1, n do
    total = total - 40000
  end
  return total
end

local function run_large_cmp(n)
  local total = 0
  for i = 1, n do
    if i < 40000 then
      total = total + 1
    end
  end
  return total
end

local arr = {}
arr[5000] = 73

local function run_large_aref(n)
  local total = 0
  for _ = 1, n do
    total = total + arr[5000]
  end
  return total
end

local add_total = expect_trace("large add immediate", function()
  return run_large_add(200)
end)
t.eq(add_total, 200 * 40000, "large add immediate result")

local sub_total = expect_trace("large sub immediate", function()
  return run_large_sub(200)
end)
t.eq(sub_total, 0, "large sub immediate result")

local cmp_total = expect_trace("large compare immediate", function()
  return run_large_cmp(200)
end)
t.eq(cmp_total, 200, "large compare immediate result")

local aref_total = expect_trace("large aref constant", function()
  return run_large_aref(200)
end)
t.eq(aref_total, 200 * 73, "large aref constant result")
