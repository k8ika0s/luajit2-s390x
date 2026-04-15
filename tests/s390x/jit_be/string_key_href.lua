local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local hit_keys = {
  "alpha",
  "bravo",
  "charlie",
  "delta",
  "echo",
  "foxtrot",
}

local miss_keys = {
  "alpha",
  "missing",
  "bravo",
  "other",
}

local map = {
  alpha = 7,
  bravo = 11,
  charlie = 13,
  delta = 17,
  echo = 19,
  foxtrot = 23,
}

local function hit_loop(n)
  local total = 0
  for i = 1, n do
    local key = hit_keys[(i - 1) % #hit_keys + 1]
    total = total + map[key]
  end
  return total
end

local function miss_loop(n)
  local total = 0
  for i = 1, n do
    local key = miss_keys[(i - 1) % #miss_keys + 1]
    total = total + (map[key] or 0)
  end
  return total
end

local function reference(fn, n)
  jit.off(fn, true)
  local result = fn(n)
  jit.on(fn, true)
  return result
end

local expected_hit = reference(hit_loop, 32000)
local expected_miss = reference(miss_loop, 32000)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

jit.flush()
local cap = t.trace_capture()
local hit = hit_loop(32000)
local miss = miss_loop(32000)
cap.stop()

t.truthy(t.find_trace_event(cap.events, "stop"), "string-key HREF traced")
t.eq(hit, expected_hit, "dynamic string-key HREF hit loop")
t.eq(miss, expected_miss, "dynamic string-key HREF miss loop")
