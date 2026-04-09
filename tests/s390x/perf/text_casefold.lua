local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 2000,
  medium = 10000,
  hot = 40000,
}

local cases = {}
for i = 1, 32 do
  cases[i] = "LuaJIT-s390x-AsciiFold-" .. string.format("%02d", i) .. "-AbCdEfGhIjKlMnOpQrStUvWxYz"
end

local function bench_lower(n)
  local total = 0
  for i = 1, n do
    local s = cases[((i - 1) % #cases) + 1]
    local out = string.lower(s)
    total = total + string.byte(out, 1) + string.byte(out, #out) + #out
  end
  return total
end

local function bench_upper(n)
  local total = 0
  for i = 1, n do
    local s = cases[((i - 1) % #cases) + 1]
    local out = string.upper(s)
    total = total + string.byte(out, 1) + string.byte(out, #out) + #out
  end
  return total
end

do
  local ok, jit = pcall(require, "jit")
  if ok and jit and jit.off then
    jit.off(bench_lower, true)
    jit.off(bench_upper, true)
  end
end

local suite = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local lower_expected = bench_lower(n)
  local upper_expected = bench_upper(n)
  suite[#suite + 1] = {
    workload = "lower_ascii",
    scale = scale,
    iterations = n,
    run = bench_lower,
    validate = function(result)
      bench.eq(result, lower_expected, "lower_ascii/" .. scale)
    end,
  }
  suite[#suite + 1] = {
    workload = "upper_ascii",
    scale = scale,
    iterations = n,
    run = bench_upper,
    validate = function(result)
      bench.eq(result, upper_expected, "upper_ascii/" .. scale)
    end,
  }
end

bench.run_suite({ family = "text_casefold", cases = suite })
