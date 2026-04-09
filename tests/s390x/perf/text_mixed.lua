local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 500,
  medium = 2000,
  hot = 8000,
}

local log_lines = {}
local sparse_lines = {}
local header_lines = {}
local control_cases = {}

for i = 1, 32 do
  local tag = string.format("%02d", i)
  local req = 300000 + i * 17
  local latency = 20 + (i * 7) % 90
  local status = 200 + (i % 5) * 10
  log_lines[i] = string.format(
    "2026-04-%sT12:%s:%sZ level=INFO req=%d path=/svc/v1/order/%s status=%d user=alpha%s zone=us-west-%d latency_ms=%d token=beta%s gamma%s",
    tag, tag, tag, req, tag, status, tag, (i % 3) + 1, latency, tag, tag
  )
  sparse_lines[i] = string.format(
    "%d -- %d .. alpha%s !! %d ?? beta%s :: %d ## gamma%s %% %d $$ delta%s",
    req, status, tag, latency, tag, req + latency, tag, req + status, tag
  )
  header_lines[i] = string.format(
    "::%s:: x-trace-%s alpha%s-token/%d",
    tag, tag, tag, req
  )
  control_cases[i] = {
    haystack = "zzneedle" .. tag .. "needlezz",
    pattern = "needle" .. tag,
    text = "LuaJIT-S390x-Mixed-" .. tag .. "-AbCdEf",
  }
end

local function bench_log_words(n)
  local total = 0
  for i = 1, n do
    local s = log_lines[((i - 1) % #log_lines) + 1]
    for tok in string.gmatch(s, "%a+") do
      total = total + #tok + string.byte(tok, 1)
    end
  end
  return total
end

local function bench_sparse_words(n)
  local total = 0
  for i = 1, n do
    local s = sparse_lines[((i - 1) % #sparse_lines) + 1]
    for tok in string.gmatch(s, "%a+") do
      total = total + #tok + string.byte(tok, 1)
    end
  end
  return total
end

local function bench_log_numbers(n)
  local total = 0
  for i = 1, n do
    local s = log_lines[((i - 1) % #log_lines) + 1]
    for tok in string.gmatch(s, "%d+") do
      total = total + #tok + string.byte(tok, 1) + string.byte(tok, #tok)
    end
  end
  return total
end

local function bench_header_match(n)
  local total = 0
  for i = 1, n do
    local s = header_lines[((i - 1) % #header_lines) + 1]
    local out = string.match(s, "%a+")
    total = total + #out + string.byte(out, 1) + string.byte(out, #out)
  end
  return total
end

local function bench_control_mix(n)
  local total = 0
  for i = 1, n do
    local case = control_cases[((i - 1) % #control_cases) + 1]
    local a, b = string.find(case.haystack, case.pattern, 1, true)
    local lower = string.lower(case.text)
    local upper = string.upper(case.text)
    total = total + a + b + #lower + #upper + string.byte(lower, 1) + string.byte(upper, #upper)
  end
  return total
end

do
  local ok, jit = pcall(require, "jit")
  if ok and jit and jit.off then
    jit.off(bench_log_words, true)
    jit.off(bench_sparse_words, true)
    jit.off(bench_log_numbers, true)
    jit.off(bench_header_match, true)
    jit.off(bench_control_mix, true)
  end
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local log_words_expected = bench_log_words(n)
  local sparse_words_expected = bench_sparse_words(n)
  local log_numbers_expected = bench_log_numbers(n)
  local header_match_expected = bench_header_match(n)
  local control_expected = bench_control_mix(n)
  cases[#cases + 1] = {
    workload = "log_words",
    scale = scale,
    iterations = n,
    run = bench_log_words,
    validate = function(result)
      bench.eq(result, log_words_expected, "log_words/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "sparse_words",
    scale = scale,
    iterations = n,
    run = bench_sparse_words,
    validate = function(result)
      bench.eq(result, sparse_words_expected, "sparse_words/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "log_numbers",
    scale = scale,
    iterations = n,
    run = bench_log_numbers,
    validate = function(result)
      bench.eq(result, log_numbers_expected, "log_numbers/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "header_match",
    scale = scale,
    iterations = n,
    run = bench_header_match,
    validate = function(result)
      bench.eq(result, header_match_expected, "header_match/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "control_mix",
    scale = scale,
    iterations = n,
    run = bench_control_mix,
    validate = function(result)
      bench.eq(result, control_expected, "control_mix/" .. scale)
    end,
  }
end

bench.run_suite({ family = "text_mixed", cases = cases })
