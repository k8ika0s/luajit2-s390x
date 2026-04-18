local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=1", "hotexit=2")

ffi.cdef[[
typedef struct { int x; int y; } pair_loop_sum_pair_t;
]]

local function pair_loop(n)
  local pair = ffi.new("pair_loop_sum_pair_t[1]")
  local total = 0
  for i = 1, n do
    pair[0].x = i
    pair[0].y = i * 2
    total = total + pair[0].x + pair[0].y
  end
  return total
end

local function pair_loop_observed(n)
  local pair = ffi.new("pair_loop_sum_pair_t[1]")
  local total = 0
  for i = 1, n do
    pair[0].x = i
    pair[0].y = i * 2
    total = total + pair[0].x + pair[0].y
  end
  return total + pair[0].x + pair[0].y
end

local function ref(fn, n)
  jit.off(fn, true)
  local result = fn(n)
  jit.on(fn, true)
  jit.flush()
  return result
end

for _, n in ipairs({1, 17, 32000, 40000}) do
  local expected = ref(pair_loop, n)
  jit.flush()
  t.eq(pair_loop(n), expected, "pair_loop n=" .. n)
end

for _, n in ipairs({1, 17, 32000}) do
  local expected = ref(pair_loop_observed, n)
  jit.flush()
  t.eq(pair_loop_observed(n), expected, "pair_loop_observed n=" .. n)
end

print("ffi_cdata_pair_loop_sum PASS")
