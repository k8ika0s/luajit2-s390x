local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
typedef enum { PROBE_RED = 11, PROBE_GREEN = 13, PROBE_BLUE = 17 } probe_color;
int64_t sum_varargs_promoted_int(int32_t seed, int count, ...);
double sum_varargs_float_cdata(double seed, int count, ...);
]]

local lib = ffi.load(libpath)
local i8 = ffi.typeof("int8_t")
local u8 = ffi.typeof("uint8_t")
local i16 = ffi.typeof("int16_t")
local u16 = ffi.typeof("uint16_t")
local color = ffi.typeof("probe_color")
local flt = ffi.typeof("float")

local function run_int(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(lib.sum_varargs_promoted_int(10, 7, i8(-1),
                                                          u8(250), i16(-2000),
                                                          u16(60000), true,
                                                          color(11), color(17)))
  end
  return total
end

local function run_float(n)
  local total = 0
  for i = 1, n do
    total = total + lib.sum_varargs_float_cdata(1.25, 4, flt(2.5), flt(3.75),
                                                flt(4.5), flt(5.25))
  end
  return total
end

jit.off(run_int, true)
jit.off(run_float, true)
local expected_int = run_int(200)
local expected_float = run_float(200)
jit.on(run_int, true)
jit.on(run_float, true)

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local capture = t.trace_capture()
local actual_int = run_int(200)
local actual_float = run_float(200)
capture.stop()

t.eq(actual_int, expected_int, "ffi promoted int vararg total")
t.approx(actual_float, expected_float, 1e-12, "ffi float cdata vararg total")
t.truthy(t.find_trace_event(capture.events, "stop"), "ffi promotion vararg traced")
