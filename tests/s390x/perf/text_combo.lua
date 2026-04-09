local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 500,
  medium = 2000,
  hot = 8000,
}

local log_lines = {}
local sparse_lines = {}
local fold_cases = {}
local control_cases = {}

for i = 1, 32 do
  local tag = string.format("%02d", i)
  local req = 400000 + i * 23
  local latency = 30 + (i * 5) % 70
  log_lines[i] = string.format(
    "2026-04-%sT13:%s:%sZ level=INFO req=%d path=/svc/v2/item/%s user=alpha%s zone=us-east-%d latency_ms=%d token=beta%s gamma%s",
    tag, tag, tag, req, tag, tag, (i % 4) + 1, latency, tag, tag
  )
  sparse_lines[i] = string.format(
    "%d -- %d .. alpha%s !! %d ?? beta%s :: %d ## gamma%s %% %d $$ delta%s",
    req, 200 + (i % 7) * 10, tag, latency, tag, req + latency, tag, req + 99, tag
  )
  fold_cases[i] = "LuaJIT-S390x-Combo-" .. tag .. "-AbCdEfGhIjKlMnOpQrStUvWxYz"
  control_cases[i] = {
    haystack = "xxneedle" .. tag .. "needlezz",
    pattern = "needle" .. tag,
    text = "s390x-control-" .. tag .. "-reverse-pass",
  }
end

local function bench_log_lower_tokens(n)
  local total = 0
  for i = 1, n do
    local s = log_lines[((i - 1) % #log_lines) + 1]
    for tok in string.gmatch(s, "%a+") do
      local folded = string.lower(tok)
      total = total + #folded + string.byte(folded, 1) + string.byte(folded, #folded)
    end
  end
  return total
end

local function bench_sparse_upper_tokens(n)
  local total = 0
  for i = 1, n do
    local s = sparse_lines[((i - 1) % #sparse_lines) + 1]
    for tok in string.gmatch(s, "%a+") do
      local folded = string.upper(tok)
      total = total + #folded + string.byte(folded, 1) + string.byte(folded, #folded)
    end
  end
  return total
end

local function bench_header_lower(n)
  local total = 0
  for i = 1, n do
    local s = log_lines[((i - 1) % #log_lines) + 1]
    local out = string.match(s, "%a+")
    local folded = string.lower(out)
    total = total + #folded + string.byte(folded, 1) + string.byte(folded, #folded)
  end
  return total
end

local function bench_casefold_bulk(n)
  local total = 0
  for i = 1, n do
    local s = fold_cases[((i - 1) % #fold_cases) + 1]
    local lower = string.lower(s)
    local upper = string.upper(s)
    total = total + #lower + #upper + string.byte(lower, 1) + string.byte(upper, #upper)
  end
  return total
end

local function bench_control_reverse_find(n)
  local total = 0
  for i = 1, n do
    local case = control_cases[((i - 1) % #control_cases) + 1]
    local a, b = string.find(case.haystack, case.pattern, 1, true)
    local rev = string.reverse(case.text)
    total = total + a + b + #rev + string.byte(rev, 1) + string.byte(rev, #rev)
  end
  return total
end

do
  local ok, jit = pcall(require, "jit")
  if ok and jit and jit.off then
    jit.off(bench_log_lower_tokens, true)
    jit.off(bench_sparse_upper_tokens, true)
    jit.off(bench_header_lower, true)
    jit.off(bench_casefold_bulk, true)
    jit.off(bench_control_reverse_find, true)
  end
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local log_lower_expected = bench_log_lower_tokens(n)
  local sparse_upper_expected = bench_sparse_upper_tokens(n)
  local header_lower_expected = bench_header_lower(n)
  local casefold_expected = bench_casefold_bulk(n)
  local control_expected = bench_control_reverse_find(n)
  cases[#cases + 1] = {
    workload = "log_lower_tokens",
    scale = scale,
    iterations = n,
    run = bench_log_lower_tokens,
    validate = function(result)
      bench.eq(result, log_lower_expected, "log_lower_tokens/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "sparse_upper_tokens",
    scale = scale,
    iterations = n,
    run = bench_sparse_upper_tokens,
    validate = function(result)
      bench.eq(result, sparse_upper_expected, "sparse_upper_tokens/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "header_lower",
    scale = scale,
    iterations = n,
    run = bench_header_lower,
    validate = function(result)
      bench.eq(result, header_lower_expected, "header_lower/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "casefold_bulk",
    scale = scale,
    iterations = n,
    run = bench_casefold_bulk,
    validate = function(result)
      bench.eq(result, casefold_expected, "casefold_bulk/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "control_reverse_find",
    scale = scale,
    iterations = n,
    run = bench_control_reverse_find,
    validate = function(result)
      bench.eq(result, control_expected, "control_reverse_find/" .. scale)
    end,
  }
end

bench.run_suite({ family = "text_combo", cases = cases })
