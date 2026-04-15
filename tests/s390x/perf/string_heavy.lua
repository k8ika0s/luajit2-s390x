local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 2000,
  medium = 8000,
  hot = 32000,
}

local search_texts = {
  "alpha-bravo-charlie-delta-echo",
  "foxtrot-golf-hotel-india-juliet",
  "kilo-lima-mike-november-oscar",
  "papa-quebec-romeo-sierra-tango",
}

local search_needles = {
  "alpha",
  "hotel",
  "november",
  "tango",
  "zulu",
}

local scan_texts = {
  "string-heavy-benchmark-0123456789-abcdefghijklmnopqrstuvwxyz",
  "LUAJIT-s390x-string-scan-ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  "search-find-byte-prefix-compare-lookup",
}

local prefix_sources = {
  "alpha-bravo-charlie-delta-echo",
  "foxtrot-golf-hotel-india-juliet",
  "kilo-lima-mike-november-oscar",
  "papa-quebec-romeo-sierra-tango",
  "string-heavy-benchmark-0123456789-abcdefghijklmnopqrstuvwxyz",
}

local prefixes = {
  "alpha",
  "foxtrot",
  "kilo",
  "papa",
  "string",
}

local lookup_keys = {
  "alpha",
  "bravo",
  "charlie",
  "delta",
  "echo",
  "foxtrot",
}

local lookup_map = {
  alpha = 7,
  bravo = 11,
  charlie = 13,
  delta = 17,
  echo = 19,
  foxtrot = 23,
}

local concat_left = {
  "alpha",
  "foxtrot",
  "kilo",
  "papa",
}

local concat_right = {
  "bravo",
  "golf",
  "lima",
  "quebec",
}

local miss_haystacks = {
  "aaaaabbbbbcccccdddddeeeee",
  "fffffggggghhhhhiiiiijjjjj",
  "kkkkklllllmmmmmnnnnnooooo",
  "pppppqqqqqrrrrrsssssttttt",
}

local miss_needles = {
  "az",
  "fy",
  "kx",
  "pw",
}

local function manual_find_loop(n)
  local total = 0
  for i = 1, n do
    local haystack = search_texts[(i - 1) % #search_texts + 1]
    local needle = search_needles[(i - 1) % #search_needles + 1]
    local needle_len = #needle
    local needle_first = string.byte(needle, 1)
    local pos = 0
    for j = 1, #haystack do
      if string.byte(haystack, j) == needle_first and haystack:sub(j, j + needle_len - 1) == needle then
        pos = j
        break
      end
    end
    total = total + pos + #haystack
  end
  return total
end

local function byte_scan_loop(n)
  local total = 0
  for i = 1, n do
    local text = scan_texts[(i - 1) % #scan_texts + 1]
    for j = 1, #text do
      total = total + string.byte(text, j)
    end
  end
  return total
end

local function prefix_eq_loop(n)
  local total = 0
  for i = 1, n do
    local text = prefix_sources[(i - 1) % #prefix_sources + 1]
    local prefix = prefixes[(i - 1) % #prefixes + 1]
    if text:sub(1, #prefix) == prefix then
      total = total + #prefix
    else
      total = total - 1
    end
  end
  return total
end

local function string_key_lookup_loop(n)
  local total = 0
  for i = 1, n do
    local key = lookup_keys[(i - 1) % #lookup_keys + 1]
    total = total + lookup_map[key]
  end
  return total
end

local function concat_slice_loop(n)
  local total = 0
  for i = 1, n do
    local left = concat_left[(i - 1) % #concat_left + 1]
    local right = concat_right[(i - 1) % #concat_right + 1]
    local value = left .. ":" .. right .. ":" .. left
    total = total + #value + string.byte(value, 1) + string.byte(value, #value)
  end
  return total
end

local function miss_find_loop(n)
  local total = 0
  for i = 1, n do
    local haystack = miss_haystacks[(i - 1) % #miss_haystacks + 1]
    local needle = miss_needles[(i - 1) % #miss_needles + 1]
    local found = string.find(haystack, needle, 1, true)
    total = total + (found or 0) + #haystack
  end
  return total
end

local function reference_result(fn, iterations)
  local ok_jit, jit = pcall(require, "jit")
  local enabled = ok_jit and jit.status()
  if ok_jit and jit.off then
    jit.off(fn, true)
  end
  local result = fn(iterations)
  if ok_jit and enabled and jit.on then
    jit.on(fn, true)
  end
  return result
end

local function force_interpreter(fn)
  return function()
    local ok_jit, jit = pcall(require, "jit")
    if ok_jit and jit.off then
      jit.off(fn, true)
    end
  end
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local expected_find = reference_result(manual_find_loop, n)
  local expected_scan = reference_result(byte_scan_loop, n)
  local expected_prefix = reference_result(prefix_eq_loop, n)
  local expected_lookup = reference_result(string_key_lookup_loop, n)
  local expected_concat = reference_result(concat_slice_loop, n)
  local expected_miss = reference_result(miss_find_loop, n)
  cases[#cases + 1] = {
    workload = "manual_find_loop",
    scale = scale,
    iterations = n,
    run = manual_find_loop,
    validate = function(result)
      bench.eq(result, expected_find, "manual_find_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "byte_scan_loop",
    scale = scale,
    iterations = n,
    run = byte_scan_loop,
    validate = function(result)
      bench.eq(result, expected_scan, "byte_scan_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "prefix_eq_loop",
    scale = scale,
    iterations = n,
    run = prefix_eq_loop,
    validate = function(result)
      bench.eq(result, expected_prefix, "prefix_eq_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "string_key_lookup_loop",
    scale = scale,
    iterations = n,
    run = string_key_lookup_loop,
    validate = function(result)
      bench.eq(result, expected_lookup, "string_key_lookup_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "concat_slice_loop",
    scale = scale,
    iterations = n,
    run = concat_slice_loop,
    validate = function(result)
      bench.eq(result, expected_concat, "concat_slice_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "miss_find_loop",
    scale = scale,
    iterations = n,
    run = miss_find_loop,
    validate = function(result)
      bench.eq(result, expected_miss, "miss_find_loop/" .. scale)
    end,
  }
end

local workload_filter = os.getenv("S390X_STRING_HEAVY_WORKLOAD")
local scale_filter = os.getenv("S390X_STRING_HEAVY_SCALE")
if (workload_filter and workload_filter ~= "") or (scale_filter and scale_filter ~= "") then
  local filtered = {}
  for _, case in ipairs(cases) do
    local workload_ok = not workload_filter or workload_filter == "" or case.workload == workload_filter
    local scale_ok = not scale_filter or scale_filter == "" or case.scale == scale_filter
    if workload_ok and scale_ok then
      filtered[#filtered + 1] = case
    end
  end
  assert(#filtered > 0, "string_heavy filter selected no cases")
  cases = filtered
end

bench.run_suite({ family = "string_heavy", cases = cases })
