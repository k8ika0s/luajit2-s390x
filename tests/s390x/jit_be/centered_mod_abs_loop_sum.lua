local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local function reference(n, mod, center)
  local total = 0
  for i = 1, n do
    local x = (i % mod) - center
    if x < 0 then
      x = -x
    end
    total = total + x
  end
  return total
end

jit.off(reference, true)

local function run_mod5(n)
  local total = 0
  for i = 1, n do
    local x = (i % 5) - 2
    if x < 0 then
      x = -x
    end
    total = total + x
  end
  return total
end

local function run_mod17(n)
  local total = 0
  for i = 1, n do
    local x = (i % 17) - 8
    if x < 0 then
      x = -x
    end
    total = total + x
  end
  return total
end

local function run_mod64(n)
  local total = 0
  for i = 1, n do
    local x = (i % 64) - 31
    if x < 0 then
      x = -x
    end
    total = total + x
  end
  return total
end

local function expect_trace(label, fn)
  jit.flush()
  local cap = t.trace_capture()
  local result = t.with_finally(function()
    cap.stop()
  end, fn)
  t.truthy(t.find_trace_event(cap.events, "stop"), label .. " traced")
  return result
end

local n = 2000
t.eq(expect_trace("centered mod 5 abs", function()
  return run_mod5(n)
end), reference(n, 5, 2), "mod5 centered abs sum")

t.eq(expect_trace("centered mod 17 abs", function()
  return run_mod17(n)
end), reference(n, 17, 8), "mod17 centered abs sum")

t.eq(expect_trace("centered mod 64 abs", function()
  return run_mod64(n)
end), reference(n, 64, 31), "mod64 centered abs sum")
