local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 2000,
  medium = 10000,
  hot = 40000,
}

local find_cases = {}
local find_miss_cases = {}
local transform_cases = {}
local compare_cases = {}

for i = 1, 32 do
  local suffix = string.format("%02d", i)
  find_cases[i] = {
    haystack = "zzneedle" .. suffix .. "needlezz",
    pattern = "needle" .. suffix,
  }
  find_miss_cases[i] = {
    haystack = string.rep("abcz", 48) .. "NEEDLe" .. suffix .. string.rep("zyxa", 24),
    pattern = "needle" .. suffix,
  }
  transform_cases[i] = "LuaJIT-S390x-MiXeD-" .. suffix
  compare_cases[i] = {
    left = "key-" .. suffix,
    right = "kez-" .. suffix,
  }
end

local function bench_find(n)
  local total = 0
  for i = 1, n do
    local case = find_cases[((i - 1) % #find_cases) + 1]
    local a, b = string.find(case.haystack, case.pattern, 1, true)
    total = total + a + b
  end
  return total
end

local function bench_find_miss(n)
  local total = 0
  for i = 1, n do
    local case = find_miss_cases[((i - 1) % #find_miss_cases) + 1]
    local a, b = string.find(case.haystack, case.pattern, 1, true)
    total = total + (a or 0) + (b or 0)
  end
  return total
end

local function bench_compare(n)
  local total = 0
  for i = 1, n do
    local case = compare_cases[((i - 1) % #compare_cases) + 1]
    if case.left < case.right then
      total = total + 1
    end
    if case.left <= case.right then
      total = total + 2
    end
  end
  return total
end

local function bench_reverse(n)
  local total = 0
  for i = 1, n do
    local s = transform_cases[((i - 1) % #transform_cases) + 1] ..
              "-reverse-pass-" .. string.format("%02d", i % 32)
    local out = string.reverse(s)
    total = total + string.byte(out, 1) + string.byte(out, #out) + #out
  end
  return total
end

local function bench_lower(n)
  local total = 0
  for i = 1, n do
    local s = transform_cases[((i - 1) % #transform_cases) + 1] ..
              "-lower-pass-" .. string.format("%02d", i % 32)
    local out = string.lower(s)
    total = total + string.byte(out, 1) + string.byte(out, #out) + #out
  end
  return total
end

local function bench_upper(n)
  local total = 0
  for i = 1, n do
    local s = transform_cases[((i - 1) % #transform_cases) + 1] ..
              "-upper-pass-" .. string.format("%02d", i % 32)
    local out = string.upper(s)
    total = total + string.byte(out, 1) + string.byte(out, #out) + #out
  end
  return total
end

do
  local ok, jit = pcall(require, "jit")
  if ok and jit and jit.off then
    jit.off(bench_find, true)
    jit.off(bench_find_miss, true)
    jit.off(bench_compare, true)
    jit.off(bench_reverse, true)
    jit.off(bench_lower, true)
    jit.off(bench_upper, true)
  end
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local find_expected = bench_find(n)
  local find_miss_expected = bench_find_miss(n)
  local compare_expected = bench_compare(n)
  local reverse_expected = bench_reverse(n)
  local lower_expected = bench_lower(n)
  local upper_expected = bench_upper(n)
  cases[#cases + 1] = {
    workload = "find_fixed",
    scale = scale,
    iterations = n,
    run = bench_find,
    validate = function(result)
      bench.eq(result, find_expected, "find_fixed/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "find_miss",
    scale = scale,
    iterations = n,
    run = bench_find_miss,
    validate = function(result)
      bench.eq(result, find_miss_expected, "find_miss/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "compare_order",
    scale = scale,
    iterations = n,
    run = bench_compare,
    validate = function(result)
      bench.eq(result, compare_expected, "compare_order/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "reverse_ascii",
    scale = scale,
    iterations = n,
    run = bench_reverse,
    validate = function(result)
      bench.eq(result, reverse_expected, "reverse_ascii/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "lower_ascii",
    scale = scale,
    iterations = n,
    run = bench_lower,
    validate = function(result)
      bench.eq(result, lower_expected, "lower_ascii/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "upper_ascii",
    scale = scale,
    iterations = n,
    run = bench_upper,
    validate = function(result)
      bench.eq(result, upper_expected, "upper_ascii/" .. scale)
    end,
  }
end

bench.run_suite({ family = "string_kernels", cases = cases })
