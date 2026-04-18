local t = require("tests.s390x.helpers.testlib")
local ffi = require("ffi")

ffi.cdef[[
int setenv(const char *name, const char *value, int overwrite);
]]
assert(ffi.C.setenv("LUAJIT_S390X_INT_MINMAX", "1", 1) == 0)

jit.opt.start("hotloop=1", "hotexit=2")

local function min_expected(n)
  local m = math.floor(n / 2)
  if n % 2 == 0 then
    return m * (m + 1)
  end
  return (m + 1) * (m + 1)
end

local function max_expected(n)
  return n * (n + 1) - min_expected(n)
end

local function load_loop(src, name)
  return assert(loadstring(src, name))()
end

local min_loop = load_loop([[
return function(n)
  local total = 0
  for i = 1, n do
    total = total + math.min(i, n + 1 - i)
  end
  return total
end
]], "@numeric_ops_min")

local max_loop = load_loop([[
return function(n)
  local total = 0
  for i = 1, n do
    total = total + math.max(i, n + 1 - i)
  end
  return total
end
]], "@numeric_ops_max")

for _, n in ipairs({1, 2, 3, 4, 127, 128, 64000, 70000}) do
  t.eq(min_loop(n), min_expected(n), "min_loop(" .. n .. ")")
  t.eq(max_loop(n), max_expected(n), "max_loop(" .. n .. ")")
end
