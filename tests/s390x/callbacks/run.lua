local ffi = require("ffi")
local t = require("tests.s390x.helpers.testlib")

local libpath = assert(arg[1], "missing callback oracle library path")

ffi.cdef([[
typedef int (*int_cb_t)(int);
typedef int (*sum6_cb_t)(int, int, int, int, int, int);
typedef double (*mix_cb_t)(double, double, int, double, int);
typedef uint64_t (*u64_cb_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

int call_once(int_cb_t cb, int value);
int call_many(int_cb_t cb, int start, int count);
int call_recursive(int_cb_t cb, int depth);
int call_sum6(sum6_cb_t cb, int a, int b, int c, int d, int e, int f);
double call_mix(mix_cb_t cb, double a, double b, int c, double d, int e);
uint64_t call_u64(u64_cb_t cb, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f);
]])

local lib = ffi.load(libpath)

local cb = ffi.cast("int_cb_t", function(value)
  collectgarbage("step", 32)
  return value * 2
end)

local ok, once = pcall(function()
  return lib.call_once(cb, 7)
end)
t.eq(ok, true, "call_once pcall")
t.eq(once, 14, "call_once")

local hook_hits = 0
debug.sethook(function()
  hook_hits = hook_hits + 1
end, "", 1)
t.eq(lib.call_once(cb, 9), 18, "call_once hook")
debug.sethook()
t.truthy(hook_hits > 0, "hook fired during callback")

t.eq(lib.call_many(cb, 1, 5), 30, "call_many")
t.eq(lib.call_recursive(cb, 4), 20, "call_recursive")

local sum6 = ffi.cast("sum6_cb_t", function(a, b, c, d, e, f)
  return a + b + c + d + e + f
end)
t.eq(lib.call_sum6(sum6, 1, 2, 3, 4, 5, 6), 21, "call_sum6")

local mix = ffi.cast("mix_cb_t", function(a, b, c, d, e)
  return a + b + d + c + e
end)
t.approx(lib.call_mix(mix, 1.25, 2.5, 3, 4.75, 5), 16.5, 1e-12, "call_mix")

local function u64(value)
  return ffi.new("uint64_t", value)
end

local u64_cb = ffi.cast("u64_cb_t", function(a, b, c, d, e, f)
  return a + b + c + d + e + f
end)
t.eq(tonumber(lib.call_u64(u64_cb, u64(1), u64(2), u64(3), u64(4), u64(5), u64(6))), 21, "call_u64")

local err_cb = ffi.cast("int_cb_t", function()
  error("callback boom")
end)
local ok_err, err_msg = pcall(function()
  return lib.call_once(err_cb, 3)
end)
t.eq(ok_err, false, "callback error propagates")
t.truthy(type(err_msg) == "string" and err_msg:find("callback boom", 1, true) ~= nil, "callback error message")

local ok_xerr, xerr_msg = xpcall(function()
  return lib.call_once(err_cb, 4)
end, function(err)
  return "wrapped:" .. tostring(err)
end)
t.eq(ok_xerr, false, "callback xpcall propagates")
t.truthy(type(xerr_msg) == "string" and xerr_msg:find("wrapped:", 1, true) ~= nil
  and xerr_msg:find("callback boom", 1, true) ~= nil, "callback xpcall message")

local nested_xpcall_cb = ffi.cast("int_cb_t", function(value)
  local ok_nested, nested_msg = xpcall(function()
    error("nested callback boom")
  end, function(err)
    return "nested:" .. tostring(err)
  end)
  t.eq(ok_nested, false, "nested xpcall status")
  t.truthy(type(nested_msg) == "string" and nested_msg:find("nested:", 1, true) ~= nil
    and nested_msg:find("nested callback boom", 1, true) ~= nil, "nested xpcall message")
  return value + 1
end)
t.eq(lib.call_once(nested_xpcall_cb, 10), 11, "nested xpcall callback")

local bad_ret_cb = ffi.cast("int_cb_t", function()
  return {}
end)
local ok_bad_ret, bad_ret_msg = pcall(function()
  return lib.call_once(bad_ret_cb, 3)
end)
t.eq(ok_bad_ret, false, "callback bad result propagates")
t.truthy(type(bad_ret_msg) == "string" and #bad_ret_msg > 0, "callback bad result message")

cb:free()
sum6:free()
mix:free()
u64_cb:free()
err_cb:free()
nested_xpcall_cb:free()
bad_ret_cb:free()
