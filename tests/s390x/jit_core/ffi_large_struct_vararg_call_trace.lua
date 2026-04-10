local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
typedef struct { uint64_t a; uint64_t b; } big_pair;
uint64_t sum_varargs_big_pair(uint64_t seed, int count, ...);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")
local pairs = {
  ffi.new("big_pair", { a = u64(1), b = u64(10) }),
  ffi.new("big_pair", { a = u64(2), b = u64(20) }),
  ffi.new("big_pair", { a = u64(3), b = u64(30) }),
  ffi.new("big_pair", { a = u64(4), b = u64(40) }),
  ffi.new("big_pair", { a = u64(5), b = u64(50) }),
  ffi.new("big_pair", { a = u64(6), b = u64(60) }),
  ffi.new("big_pair", { a = u64(7), b = u64(70) }),
}

local function run(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(lib.sum_varargs_big_pair(u64(i), 7, pairs[1],
                                                      pairs[2], pairs[3],
                                                      pairs[4], pairs[5],
                                                      pairs[6], pairs[7]))
  end
  return total
end

jit.off(run, true)
local expected = run(200)
jit.on(run, true)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local capture = t.trace_capture()
local actual = run(200)
capture.stop()

t.eq(actual, expected, "ffi large struct vararg total")
t.truthy(t.find_trace_event(capture.events, "stop"), "ffi large struct vararg traced")
