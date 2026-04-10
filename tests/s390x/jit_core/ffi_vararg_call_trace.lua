local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
uint64_t sum_varargs(uint64_t seed, int count, ...);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")

local function run(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(lib.sum_varargs(u64(i), 4, u64(1), u64(2),
                                             u64(3), u64(4)))
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

t.eq(tonumber(actual), tonumber(expected), "ffi integer vararg total")
t.truthy(t.find_trace_event(capture.events, "stop"), "ffi integer vararg traced")
