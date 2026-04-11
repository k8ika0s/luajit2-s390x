local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")
local ffi = require("ffi")

ffi.cdef[[
int setenv(const char *name, const char *value, int overwrite);
]]
assert(ffi.C.setenv("LUAJIT_S390X_INT_MINMAX", "1", 1) == 0,
       "setenv minmax")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local function expect_trace(label, fn)
  jit.flush()
  local cap = t.trace_capture()
  local ok, a, b, c, d = t.with_finally(function()
    cap.stop()
  end, fn)
  t.truthy(t.find_trace_event(cap.events, "stop"), label .. " traced")
  return ok, a, b, c, d
end

local function sum_loop(n)
  local total = 0
  for i = 1, n do
    total = total + i
  end
  return total
end

local function sub_loop(n)
  local total = 0
  for i = 1, n do
    total = total - i
  end
  return total
end

local function max_loop(n)
  local total = 0
  for i = 1, n do
    total = total + math.max(i, n + 1 - i)
  end
  return total
end

local function run_sum_boundaries()
  return sum_loop(65535), sum_loop(65536), sum_loop(65537), sum_loop(70000)
end

local function run_sub_boundaries()
  return sub_loop(65535), sub_loop(65536), sub_loop(65537), sub_loop(70000)
end

local function run_max_boundaries()
  return max_loop(60000), max_loop(64000), max_loop(70000), max_loop(80000)
end

local s65535, s65536, s65537, s70000 = expect_trace("ADDOV boundary", run_sum_boundaries)
t.eq(s65535, 2147450880, "sum_loop(65535)")
t.eq(s65536, 2147516416, "sum_loop(65536)")
t.eq(s65537, 2147581953, "sum_loop(65537)")
t.eq(s70000, 2450035000, "sum_loop(70000)")

local d65535, d65536, d65537, d70000 = expect_trace("SUBOV boundary", run_sub_boundaries)
t.eq(d65535, -2147450880, "sub_loop(65535)")
t.eq(d65536, -2147516416, "sub_loop(65536)")
t.eq(d65537, -2147581953, "sub_loop(65537)")
t.eq(d70000, -2450035000, "sub_loop(70000)")

local m60000, m64000, m70000, m80000 =
  expect_trace("ADDOV commuted max boundary", run_max_boundaries)
t.eq(m60000, 2700030000, "max_loop(60000)")
t.eq(m64000, 3072032000, "max_loop(64000)")
t.eq(m70000, 3675035000, "max_loop(70000)")
t.eq(m80000, 4800040000, "max_loop(80000)")
