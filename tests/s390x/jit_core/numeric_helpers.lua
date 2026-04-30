local bit = require("bit")
local have_ffi, ffi = pcall(require, "ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local unpack = unpack
local INT_MAX = 2147483647

local function traced(label, fn, ...)
  local args = { ... }
  local nargs = select("#", ...)
  jit.flush()
  local cap = t.trace_capture()
  local results = { t.with_finally(function()
    cap.stop()
  end, function()
    return fn(unpack(args, 1, nargs))
  end) }
  local stop_ev = t.assert_trace_stop(cap.events, label .. " traced")
  local traceno = tonumber(stop_ev[2])
  t.truthy(traceno and #t.traceinfo_snapshot(traceno) >= 1,
           label .. " traceinfo")
  jit.flush()
  return unpack(results)
end

local function interp(fn, ...)
  local args = { ... }
  local nargs = select("#", ...)
  jit.off(fn, true)
  local results = { fn(unpack(args, 1, nargs)) }
  jit.on(fn, true)
  return unpack(results)
end

local function run()
  local abs_total = 0
  local min_total = 0
  local max_total = 0
  local div_total = 0
  local sqrt_total = 0
  local tobit_total = 0

  for i = 1, 200 do
    local signed = (i % 2 == 0) and -i or i
    abs_total = abs_total + math.abs(signed)
    min_total = min_total + math.min(i, 201 - i)
    max_total = max_total + math.max(i, 201 - i)
    div_total = div_total + ((i + 0.5) / (i + 1.25))
    sqrt_total = sqrt_total + math.sqrt(i + 0.25)
    tobit_total = tobit_total + bit.tobit(i * 65539)
  end

  return {
    abs_total = abs_total,
    min_total = min_total,
    max_total = max_total,
    div_total = div_total,
    sqrt_total = sqrt_total,
    tobit_total = tobit_total,
  }
end

local function run_abs_conv_edges(n)
  local signed_total = 0
  local mixed_total = 0
  local source_total = 0
  local zero_sign = 0

  for i = 0, n do
    local y = 0 - i
    signed_total = signed_total + math.abs(i)
    mixed_total = mixed_total + math.abs(y) + y
    source_total = source_total + math.abs(y) + y + i
    if i == 0 and 1 / math.abs(y) > 0 then
      zero_sign = zero_sign + 1
    end
  end

  return signed_total, mixed_total, source_total, zero_sign
end

local function run_abs_dynamic_stop_offset(stop)
  local total = 0
  local offset = 13

  if stop > INT_MAX - offset then
    stop = INT_MAX - offset
  end
  if stop < 0 then
    return 0
  end

  for i = 0, stop do
    local x = i + offset
    local y = 0 - x
    total = total + math.abs(y) + y + x
  end

  return total
end

local function run_abs_intmax_boundary()
  local total = 0
  local first = INT_MAX - 200

  for i = first, INT_MAX do
    local y = 0 - i
    total = total + math.abs(y) + y + (i - first)
  end

  return total
end

local function run_abs_intmax_offset_reject()
  local total = 0
  local first = INT_MAX - 200

  for i = first, INT_MAX do
    local x = i + 1
    local y = 0 - x
    total = total + math.abs(y) + y
    if x > INT_MAX then
      total = total + 1
    end
  end

  return total
end

local function run_abs_unsigned_edges(reps)
  local u32 = ffi.new("uint32_t[5]", 0, 1, 0x7fffffff, 0x80000000, 0xffffffff)
  local u16 = ffi.new("uint16_t[3]", 0, 0x7fff, 0xffff)
  local u8 = ffi.new("uint8_t[3]", 0, 0x7f, 0xff)
  local total = 0

  for _ = 1, reps do
    for i = 0, 4 do
      total = total + math.abs(u32[i]) - tonumber(u32[i])
    end
    for i = 0, 2 do
      total = total + math.abs(u16[i]) - tonumber(u16[i])
      total = total + math.abs(u8[i]) - tonumber(u8[i])
    end
  end

  return total
end

local function run_fp_intconv_left_edges(n)
  local sub_total = 0
  local mul_total = 0
  local div_total = 0
  local live_total = 0

  for i = 1, n do
    local x = (i % 2 == 0) and i or -i
    local den = (i % 9) + 1.25
    local rhs = (i % 7) + 0.5
    local q = x / den
    local s = x - rhs
    local m = x * rhs
    sub_total = sub_total + s
    mul_total = mul_total + m
    div_total = div_total + q
    live_total = live_total + x + (x - 0.25)
    live_total = live_total + q + s + m + (x * 0.75)
  end

  return sub_total, mul_total, div_total, live_total
end

local function run_mod_power2_edges(n)
  local nonnegative_total = 0
  local signed_total = 0

  for i = 0, n do
    nonnegative_total = nonnegative_total + (i % 2) + (i % 4)
    nonnegative_total = nonnegative_total + ((i + 3) % 8)
    signed_total = signed_total + ((0 - i) % 8)
  end

  return nonnegative_total, signed_total
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local expected = interp(run)
local actual = traced("numeric helpers", run)
local edge_expected = { interp(run_abs_conv_edges, 200) }
local edge_actual = { traced("abs conv edges", run_abs_conv_edges, 200) }
local dynamic_expected = interp(run_abs_dynamic_stop_offset, 200)
local dynamic_actual = traced("abs dynamic stop offset",
                              run_abs_dynamic_stop_offset, 200)
local boundary_expected = interp(run_abs_intmax_boundary)
local boundary_actual = traced("abs intmax boundary", run_abs_intmax_boundary)
local reject_expected = interp(run_abs_intmax_offset_reject)
jit.flush()
local reject_actual = run_abs_intmax_offset_reject()
jit.flush()
local unsigned_expected = have_ffi and interp(run_abs_unsigned_edges, 50) or 0
local unsigned_delta = have_ffi and traced("abs unsigned conv high bits",
                                           run_abs_unsigned_edges, 50) or 0
local fp_intconv_expected = { interp(run_fp_intconv_left_edges, 200) }
local fp_intconv_actual = { traced("fp intconv left edges",
                                   run_fp_intconv_left_edges, 200) }
local mod_power2_expected = { interp(run_mod_power2_edges, 200) }
local mod_power2_actual = { traced("mod power2 edges",
                                   run_mod_power2_edges, 200) }

t.eq(actual.abs_total, expected.abs_total, "abs total")
t.eq(actual.min_total, expected.min_total, "min total")
t.eq(actual.max_total, expected.max_total, "max total")
t.approx(actual.div_total, expected.div_total, 1e-12, "div total")
t.approx(actual.sqrt_total, expected.sqrt_total, 1e-12, "sqrt total")
t.eq(actual.tobit_total, expected.tobit_total, "tobit total")
t.eq(edge_actual[1], edge_expected[1], "abs nonnegative conv total")
t.eq(edge_actual[2], edge_expected[2], "abs negative conv mixed use")
t.eq(edge_actual[3], edge_expected[3], "subov source remains live")
t.eq(edge_actual[4], edge_expected[4], "abs negative zero avoided")
t.eq(edge_actual[1], 20100, "abs nonnegative conv expected total")
t.eq(edge_actual[2], 0, "abs negative conv expected mixed use")
t.eq(edge_actual[3], 20100, "subov source expected live")
t.eq(edge_actual[4], 1, "abs negative zero expected sign")
t.eq(dynamic_actual, dynamic_expected, "abs dynamic stop offset")
t.eq(dynamic_actual, 22713, "abs dynamic stop expected total")
t.eq(boundary_actual, boundary_expected, "abs intmax boundary")
t.eq(boundary_actual, 20100, "abs intmax boundary expected total")
t.eq(reject_actual, reject_expected, "abs intmax offset reject")
t.eq(reject_actual, 1, "abs intmax offset reject expected total")
if have_ffi then
  t.eq(unsigned_delta, unsigned_expected, "abs unsigned conv high bits")
  t.eq(unsigned_delta, 0, "abs unsigned conv high bits expected delta")
end
t.approx(fp_intconv_actual[1], fp_intconv_expected[1], 1e-12,
         "fp intconv sub left")
t.approx(fp_intconv_actual[2], fp_intconv_expected[2], 1e-12,
         "fp intconv mul left")
t.approx(fp_intconv_actual[3], fp_intconv_expected[3], 1e-12,
         "fp intconv div left")
t.approx(fp_intconv_actual[4], fp_intconv_expected[4], 1e-12,
         "fp intconv source live and reuse")
t.eq(mod_power2_actual[1], mod_power2_expected[1],
     "mod power2 nonnegative total")
t.eq(mod_power2_actual[2], mod_power2_expected[2],
     "mod power2 signed fallback total")
