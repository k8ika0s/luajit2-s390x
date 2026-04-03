local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

ffi.cdef[[
int abs(int x);
]]

local cabs = ffi.C.abs

local function direct_abs_literal_stop_real()
  local total = 0
  for i = 1, 80000 do
    total = total + ffi.C.abs((i % 17) - 8)
  end
  return total
end

local function stored_abs_literal_stop_real()
  local total = 0
  for i = 1, 80000 do
    total = total + cabs((i % 17) - 8)
  end
  return total
end

local function drive(fn)
  local out
  for i = 1, 4 do
    out = fn()
  end
  return out
end

local function expect(fn, label)
  jit.off(fn, true)
  jit.off(drive, true)
  local expected = drive(fn)
  jit.on(fn, true)
  jit.on(drive, true)
  jit.flush()
  jit.opt.start("hotloop=1", "hotexit=1")
  local actual = drive(fn)
  t.eq(actual, expected, label)
end

expect(direct_abs_literal_stop_real, "ffi direct literal-stop same-callsite")
expect(stored_abs_literal_stop_real, "ffi stored literal-stop same-callsite")
