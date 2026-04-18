local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local function expect_trace(label, fn)
  jit.flush()
  local cap = t.trace_capture()
  local ok, result = t.with_finally(function()
    cap.stop()
  end, fn)
  t.truthy(t.find_trace_event(cap.events, "stop"), label .. " traced")
  return ok, result
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local function run_abs(n)
  local total = 0
  for i = 1, n do
    local signed = (i % 2 == 0) and -i or i
    total = total + math.abs(signed)
  end
  return total
end

local function run_div(n)
  local total = 0
  for i = 1, n do
    total = total + ((i + 0.5) / (i + 1.25))
  end
  return total
end

local function run_fp_mod(n)
  local total = 0
  for i = 1, n do
    total = total + ((i + 0.25) % 7.5) + ((-i - 0.5) % 5.25)
  end
  return total
end

local function run_minmax(n)
  local min_total = 0
  local max_total = 0
  for i = 1, n do
    min_total = min_total + math.min(i, n + 1 - i)
    max_total = max_total + math.max(i, n + 1 - i)
  end
  return min_total, max_total
end

local abs_total = expect_trace("math.abs loop", function()
  return run_abs(200)
end)
t.eq(abs_total, 20100, "math.abs total")

local div_expected = run_div(200)
local div_total = expect_trace("fp divide loop", function()
  return run_div(200)
end)
t.approx(div_total, div_expected, 1e-12, "fp divide total")

local fp_mod_expected = run_fp_mod(500)
local fp_mod_total = expect_trace("fp modulo loop", function()
  return run_fp_mod(500)
end)
t.approx(fp_mod_total, fp_mod_expected, 1e-9, "fp modulo total")

local min_expected, max_expected = run_minmax(200)
local min_total, max_total = expect_trace("math.min/math.max loop", function()
  return run_minmax(200)
end)
t.eq(min_total, min_expected, "math.min total")
t.eq(max_total, max_expected, "math.max total")
