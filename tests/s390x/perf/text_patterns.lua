local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 500,
  medium = 2000,
  hot = 8000,
}

local words = {}
local words_seek = {}
local digits = {}
local digits_seek = {}
local text = {}
local text_sparse = {}

for i = 1, 32 do
  local tag = string.format("%02d", i)
  words[i] = "AlphaBetaGamma" .. tag .. "tail"
  words_seek[i] = "__" .. tag .. "--" .. words[i]
  digits[i] = "1234567890" .. tag .. "xyz"
  digits_seek[i] = "alpha" .. tag .. "--" .. digits[i]
  text[i] = table.concat({
    "alpha", tag, " beta", tag, " gamma", tag,
    " 123", tag, " delta", tag, " epsilon", tag,
    " 456", tag, " zeta", tag, " eta", tag,
  })
  text_sparse[i] = table.concat({
    "123", tag, " -- alpha", tag, " !! ",
    "456", tag, " ?? beta", tag, " :: ",
    "789", tag, " ## gamma", tag, " %% ",
    "012", tag, " $$ delta", tag,
  })
end

local function bench_match_alpha(n)
  local total = 0
  for i = 1, n do
    local s = words[((i - 1) % #words) + 1]
    local out = string.match(s, "%a+")
    total = total + #out + string.byte(out, 1) + string.byte(out, #out)
  end
  return total
end

local function bench_match_digit(n)
  local total = 0
  for i = 1, n do
    local s = digits[((i - 1) % #digits) + 1]
    local out = string.match(s, "%d+")
    total = total + #out + string.byte(out, 1) + string.byte(out, #out)
  end
  return total
end

local function bench_match_alpha_seek(n)
  local total = 0
  for i = 1, n do
    local s = words_seek[((i - 1) % #words_seek) + 1]
    local out = string.match(s, "%a+")
    total = total + #out + string.byte(out, 1) + string.byte(out, #out)
  end
  return total
end

local function bench_match_digit_seek(n)
  local total = 0
  for i = 1, n do
    local s = digits_seek[((i - 1) % #digits_seek) + 1]
    local out = string.match(s, "%d+")
    total = total + #out + string.byte(out, 1) + string.byte(out, #out)
  end
  return total
end

local function bench_gmatch_words(n)
  local total = 0
  for i = 1, n do
    local s = text[((i - 1) % #text) + 1]
    for tok in string.gmatch(s, "%a+") do
      total = total + #tok
    end
  end
  return total
end

local function bench_gmatch_words_sparse(n)
  local total = 0
  for i = 1, n do
    local s = text_sparse[((i - 1) % #text_sparse) + 1]
    for tok in string.gmatch(s, "%a+") do
      total = total + #tok
    end
  end
  return total
end

do
  local ok, jit = pcall(require, "jit")
  if ok and jit and jit.off then
    jit.off(bench_match_alpha, true)
    jit.off(bench_match_digit, true)
    jit.off(bench_match_alpha_seek, true)
    jit.off(bench_match_digit_seek, true)
    jit.off(bench_gmatch_words, true)
    jit.off(bench_gmatch_words_sparse, true)
  end
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local alpha_expected = bench_match_alpha(n)
  local digit_expected = bench_match_digit(n)
  local alpha_seek_expected = bench_match_alpha_seek(n)
  local digit_seek_expected = bench_match_digit_seek(n)
  local gmatch_expected = bench_gmatch_words(n)
  local gmatch_sparse_expected = bench_gmatch_words_sparse(n)
  cases[#cases + 1] = {
    workload = "match_alpha",
    scale = scale,
    iterations = n,
    run = bench_match_alpha,
    validate = function(result)
      bench.eq(result, alpha_expected, "match_alpha/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "match_digit",
    scale = scale,
    iterations = n,
    run = bench_match_digit,
    validate = function(result)
      bench.eq(result, digit_expected, "match_digit/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "match_alpha_seek",
    scale = scale,
    iterations = n,
    run = bench_match_alpha_seek,
    validate = function(result)
      bench.eq(result, alpha_seek_expected, "match_alpha_seek/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "match_digit_seek",
    scale = scale,
    iterations = n,
    run = bench_match_digit_seek,
    validate = function(result)
      bench.eq(result, digit_seek_expected, "match_digit_seek/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "gmatch_words",
    scale = scale,
    iterations = n,
    run = bench_gmatch_words,
    validate = function(result)
      bench.eq(result, gmatch_expected, "gmatch_words/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "gmatch_words_sparse",
    scale = scale,
    iterations = n,
    run = bench_gmatch_words_sparse,
    validate = function(result)
      bench.eq(result, gmatch_sparse_expected, "gmatch_words_sparse/" .. scale)
    end,
  }
end

bench.run_suite({ family = "text_patterns", cases = cases })
