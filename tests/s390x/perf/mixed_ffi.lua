local ffi = require("ffi")
local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

ffi.cdef[[
int abs(int x);
typedef struct { int x; int y; } pair_t;
]]

local scales = {
  small = 1000,
  medium = 4000,
  hot = 16000,
}

local function mixed_ffi_loop(n)
  local total = 0
  local pair = ffi.new("pair_t[1]")
  for i = 1, n do
    pair[0].x = ffi.C.abs((i % 19) - 9)
    pair[0].y = bit.band(i * 33, 0x3ff)
    total = total + pair[0].x + pair[0].y + select(((i - 1) % 3) + 1, 3, 5, 7)
  end
  return total
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local expected = mixed_ffi_loop(n)
  cases[#cases + 1] = {
    workload = "mixed_ffi_loop",
    scale = scale,
    iterations = n,
    run = mixed_ffi_loop,
    validate = function(result)
      bench.eq(result, expected, "mixed_ffi_loop/" .. scale)
    end,
  }
end

bench.run_suite({ family = "mixed_ffi", cases = cases })
