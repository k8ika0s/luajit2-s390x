local ffi = require("ffi")
local bench = dofile("tests/s390x/perf/benchlib.lua")
local buffer = require("string.buffer")

ffi.cdef[[
typedef struct { int x; int y; } pair_t;
typedef struct { unsigned short a; unsigned int b; unsigned char c; } packed_u_t;
]]

local scales = {
  small = 2000,
  medium = 8000,
  hot = 32000,
}

local function pair_loop(n)
  local pair = ffi.new("pair_t[1]")
  local total = 0
  for i = 1, n do
    pair[0].x = i
    pair[0].y = i * 2
    total = total + pair[0].x + pair[0].y
  end
  return total
end

local function mixed_width_loop(n)
  local slot = ffi.new("packed_u_t[1]")
  local total = 0
  for i = 1, n do
    slot[0].a = i % 65535
    slot[0].b = (i % 4096) * 17
    slot[0].c = i % 251
    total = total + slot[0].a + slot[0].b + slot[0].c
  end
  return total
end

local function buffer_fref_loop(n)
  local buf = buffer.new()
  local total = 0
  for i = 1, n do
    buf:reset()
    buf:put("abcdef")
    buf:skip(i % 3)
    total = total + #buf
  end
  return total
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local expected_pair = pair_loop(n)
  local expected_width = mixed_width_loop(n)
  local expected_buffer = buffer_fref_loop(n)
  cases[#cases + 1] = {
    workload = "pair_loop",
    scale = scale,
    iterations = n,
    run = pair_loop,
    validate = function(result)
      bench.eq(result, expected_pair, "pair_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "mixed_width_loop",
    scale = scale,
    iterations = n,
    run = mixed_width_loop,
    validate = function(result)
      bench.eq(result, expected_width, "mixed_width_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "buffer_fref_loop",
    scale = scale,
    iterations = n,
    run = buffer_fref_loop,
    validate = function(result)
      bench.eq(result, expected_buffer, "buffer_fref_loop/" .. scale)
    end,
  }
end

bench.run_suite({ family = "ffi_cdata", cases = cases })
