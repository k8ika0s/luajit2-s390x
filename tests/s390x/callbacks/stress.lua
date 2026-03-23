local ffi = require("ffi")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/callbacks/build/libcallback_oracle.so"

ffi.cdef([[
typedef int (*int_cb_t)(int);
int call_once(int_cb_t cb, int value);
int call_many(int_cb_t cb, int start, int count);
]])

local lib = ffi.load(libpath)
local hook_hits = 0
local callback_hits = 0

debug.sethook(function()
  hook_hits = hook_hits + 1
end, "", 1)

local cb = ffi.cast("int_cb_t", function(value)
  callback_hits = callback_hits + 1
  local scratch = {}
  for i = 1, 6 do
    scratch[i] = { value = value + i }
  end
  if value % 5 == 0 then
    collectgarbage("collect")
  else
    collectgarbage("step", 64)
  end
  return value + 3
end)

local many = lib.call_many(cb, 1, 40)
local once = lib.call_once(cb, 99)

debug.sethook()

t.eq(many, 940, "callback stress many")
t.eq(once, 102, "callback stress once")
t.eq(callback_hits, 41, "callback hit count")
t.truthy(hook_hits > 0, "callback hook churn")

cb:free()
