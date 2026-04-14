local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local values = { 1.25, 2.5, 3.75, 4.125 }
local keys = { "a", "b", "c", "d" }
local hash_values = { a = 1.25, b = 2.5, c = 3.75, d = 4.125 }
local upvalue = 1.75

local function set_upvalue(value)
  upvalue = value
end

local function num_aload_loop(n)
  local total = 0
  for i = 1, n do
    total = total + values[(i % #values) + 1]
  end
  return total
end

local function num_hload_loop(n)
  local total = 0
  for i = 1, n do
    total = total + hash_values[keys[(i % #keys) + 1]]
  end
  return total
end

local function num_uload_loop(n)
  local total = 0
  for i = 1, n do
    total = total + upvalue
  end
  return total
end

local function num_vload_loop(n, ...)
  local total = 0
  for i = 1, n do
    local a, b, c, d = ...
    local idx = (i % 4) + 1
    if idx == 1 then
      total = total + a
    elseif idx == 2 then
      total = total + b
    elseif idx == 3 then
      total = total + c
    else
      total = total + d
    end
  end
  return total
end

local function expected_pattern_total(n)
  return n * (1.25 + 2.5 + 3.75 + 4.125) / #values
end

local function run_traced(label, fn, expected)
  jit.flush()
  local cap = t.trace_capture()
  local result = t.with_finally(function()
    cap.stop()
  end, fn)
  t.assert_trace_stop(cap.events, label .. " trace")
  t.approx(result, expected, 1e-12, label .. " total")
end

run_traced("numeric ALOAD", function()
  return num_aload_loop(400)
end, expected_pattern_total(400))

run_traced("numeric HLOAD", function()
  return num_hload_loop(400)
end, expected_pattern_total(400))

set_upvalue(1.75)
run_traced("numeric ULOAD", function()
  return num_uload_loop(400)
end, 400 * 1.75)

run_traced("numeric VLOAD", function()
  return num_vload_loop(400, 1.25, 2.5, 3.75, 4.125)
end, expected_pattern_total(400))
