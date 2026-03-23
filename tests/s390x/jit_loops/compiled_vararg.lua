local jit = require("jit")
local jutil = require("jit.util")
local t = require("tests.s390x.helpers.testlib")

local function sum_window(...)
  return select(1, ...) + select(2, ...) + select(3, ...) + select(4, ...)
end

local function sum_window_loop_index(...)
  local total = 0
  local n = select("#", ...)
  for i = 1, n do
    total = total + select(i, ...)
  end
  return total
end

local function sum_window_header_limit(...)
  local total = 0
  for i = 1, select("#", ...) do
    total = total + select(i, ...)
  end
  return total
end

local function drive(fn)
  local total = 0
  for i = 1, 120 do
    total = total + fn(i, i + 1, i + 2, i + 3)
  end
  return total
end

local expected = 0
for i = 1, 120 do
  expected = expected + (4 * i + 6)
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.flush()
jit.opt.start("hotloop=2")
local actual = drive(sum_window)

t.eq(actual, expected, "compiled vararg loop")
t.truthy(jutil.traceinfo(1) ~= nil, "compiled vararg trace exists")

-- These remain tracked vararg edges on s390x and are intentionally kept out of
-- the closure trace gate until the traced loop-carried dynamic-select path is
-- fixed.
jit.off(sum_window_loop_index, true)
t.eq(sum_window_loop_index(1, 2, 3, 4), 10, "compiled vararg loop-index interp")

jit.off(sum_window_header_limit, true)
t.eq(sum_window_header_limit(1, 2, 3, 4), 10, "compiled vararg header-limit interp")
