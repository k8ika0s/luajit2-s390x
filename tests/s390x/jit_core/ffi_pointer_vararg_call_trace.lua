local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
typedef int32_t (*i32_callback)(int32_t);
uint64_t sum_varargs_ptr_values(uint64_t seed, int count, ...);
int64_t sum_varargs_i32_callbacks(int32_t seed, int count, ...);
int32_t echo_i32(int32_t value);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")
local intptr = ffi.typeof("int32_t *")
local a = ffi.new("int32_t[1]", { 7 })
local b = ffi.new("int32_t[1]", { 11 })
local c = ffi.new("int32_t[1]", { 13 })

local function run_ptr(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(lib.sum_varargs_ptr_values(u64(i), 4, a,
                                                        ffi.cast(intptr, b),
                                                        nil, c))
  end
  return total
end

local function run_callback(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(lib.sum_varargs_i32_callbacks(10, 3, lib.echo_i32,
                                                           lib.echo_i32,
                                                           lib.echo_i32))
  end
  return total
end

jit.off(run_ptr, true)
jit.off(run_callback, true)
local expected_ptr = run_ptr(200)
local expected_callback = run_callback(200)
jit.on(run_ptr, true)
jit.on(run_callback, true)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local capture = t.trace_capture()
local actual_ptr = run_ptr(200)
local actual_callback = run_callback(200)
capture.stop()

t.eq(actual_ptr, expected_ptr, "ffi pointer vararg total")
t.eq(actual_callback, expected_callback, "ffi callback vararg total")
t.truthy(t.find_trace_event(capture.events, "stop"), "ffi pointer vararg traced")
