local ffi = require("ffi")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local libpath = arg[1] or "tests/s390x/ffi_abi/build/liboracle.so"

ffi.cdef[[
typedef struct { uint32_t a; } small_u32;
typedef struct { uint32_t a; uint32_t b; } small_u64;
typedef struct { float a; } one_float;
typedef struct { double a; } one_double;
typedef struct { uint64_t a; uint64_t b; } big_pair;
typedef struct { double a; double b; } hfa2d;

__attribute__((const)) uint64_t take_small_u32(small_u32 value);
__attribute__((const)) uint64_t take_small_u64(small_u64 value);
__attribute__((const)) double take_one_float(one_float value);
__attribute__((const)) double take_one_double(one_double value);
__attribute__((const)) uint64_t take_big_pair(big_pair value);
__attribute__((const)) double take_hfa2d(hfa2d value);
__attribute__((const)) uint64_t take6_small_u32(small_u32 a, small_u32 b, small_u32 c,
                         small_u32 d, small_u32 e, small_u32 f);
__attribute__((const)) uint64_t take7_small_u32(small_u32 a, small_u32 b, small_u32 c,
                         small_u32 d, small_u32 e, small_u32 f,
                         small_u32 g);
__attribute__((const)) uint64_t take6_small_u64(small_u64 a, small_u64 b, small_u64 c,
                         small_u64 d, small_u64 e, small_u64 f);
__attribute__((const)) uint64_t take7_small_u64(small_u64 a, small_u64 b, small_u64 c,
                         small_u64 d, small_u64 e, small_u64 f,
                         small_u64 g);
__attribute__((const)) double take6_one_double(one_double a, one_double b, one_double c,
                        one_double d, one_double e, one_double f);
__attribute__((const)) double take7_one_double(one_double a, one_double b, one_double c,
                        one_double d, one_double e, one_double f,
                        one_double g);
]]

local lib = ffi.load(libpath)
local u64 = ffi.typeof("uint64_t")

local small32 = ffi.new("small_u32", { a = 0x12345678 })
local small64 = ffi.new("small_u64", { a = 0x11111111, b = 0x22222222 })
local one_float = ffi.new("one_float", { a = 3.5 })
local one_double = ffi.new("one_double", { a = 4.25 })
local big_pair = ffi.new("big_pair", { a = u64(7000), b = u64(8000) })
local hfa2d = ffi.new("hfa2d", { a = 1.25, b = 2.5 })

local take_small_u32 = lib.take_small_u32
local take_small_u64 = lib.take_small_u64
local take_one_float = lib.take_one_float
local take_one_double = lib.take_one_double
local take_big_pair = lib.take_big_pair
local take_hfa2d = lib.take_hfa2d
local take6_small_u32 = lib.take6_small_u32
local take7_small_u32 = lib.take7_small_u32
local take6_small_u64 = lib.take6_small_u64
local take7_small_u64 = lib.take7_small_u64
local take6_one_double = lib.take6_one_double
local take7_one_double = lib.take7_one_double

local scales = {
  small = 4000,
  medium = 20000,
  hot = 80000,
}

local function small_u32_call(n)
  local total = 0
  for _ = 1, n do
    total = total + tonumber(take_small_u32(small32))
  end
  return total
end

local function small_u64_call(n)
  local total = 0
  for _ = 1, n do
    total = total + tonumber(take_small_u64(small64))
  end
  return total
end

local function one_float_call(n)
  local total = 0
  for _ = 1, n do
    total = total + take_one_float(one_float)
  end
  return total
end

local function one_double_call(n)
  local total = 0
  for _ = 1, n do
    total = total + take_one_double(one_double)
  end
  return total
end

local function big_pair_call(n)
  local total = 0
  for _ = 1, n do
    total = total + tonumber(take_big_pair(big_pair))
  end
  return total
end

local function hfa2d_call(n)
  local total = 0
  for _ = 1, n do
    total = total + take_hfa2d(hfa2d)
  end
  return total
end

local function small_u32_take6(n)
  local total = 0
  for _ = 1, n do
    total = total + tonumber(take6_small_u32(
      small32, small32, small32, small32, small32, small32))
  end
  return total
end

local function small_u32_take7(n)
  local total = 0
  for _ = 1, n do
    total = total + tonumber(take7_small_u32(
      small32, small32, small32, small32, small32, small32, small32))
  end
  return total
end

local function small_u64_take6(n)
  local total = 0
  for _ = 1, n do
    total = total + tonumber(take6_small_u64(
      small64, small64, small64, small64, small64, small64))
  end
  return total
end

local function small_u64_take7(n)
  local total = 0
  for _ = 1, n do
    total = total + tonumber(take7_small_u64(
      small64, small64, small64, small64, small64, small64, small64))
  end
  return total
end

local function one_double_take6(n)
  local total = 0
  for _ = 1, n do
    total = total + take6_one_double(
      one_double, one_double, one_double, one_double, one_double, one_double)
  end
  return total
end

local function one_double_take7(n)
  local total = 0
  for _ = 1, n do
    total = total + take7_one_double(
      one_double, one_double, one_double, one_double, one_double, one_double,
      one_double)
  end
  return total
end

local workloads = {
  { "small_u32_call", small_u32_call },
  { "small_u64_call", small_u64_call },
  { "one_float_call", one_float_call },
  { "one_double_call", one_double_call },
  { "big_pair_call", big_pair_call },
  { "hfa2d_call", hfa2d_call },
  { "small_u32_take6", small_u32_take6 },
  { "small_u32_take7", small_u32_take7 },
  { "small_u64_take6", small_u64_take6 },
  { "small_u64_take7", small_u64_take7 },
  { "one_double_take6", one_double_take6 },
  { "one_double_take7", one_double_take7 },
}

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  for _, workload in ipairs(workloads) do
    local name = workload[1]
    local run = workload[2]
    local expected = run(n)
    cases[#cases + 1] = {
      workload = name,
      scale = scale,
      iterations = n,
      run = run,
      validate = function(result)
        bench.eq(result, expected, name .. "/" .. scale)
      end,
    }
  end
end

bench.run_suite({ family = "ffi_fixed_struct_calls", cases = cases })
