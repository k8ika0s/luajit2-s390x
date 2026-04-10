local bench = dofile("tests/s390x/perf/benchlib.lua")
local decimal = require("s390x.experimental.decimal")

local scales = {
  small = 2000,
  medium = 10000,
  hot = 40000,
}

local decimal_strings = {
  "12345.67",
  "-98765.43",
  "100200300.40",
  "-0.125",
}

local packed_terms = {
  decimal.to_packed(decimal.new("12345.67"), 7),
  decimal.to_packed(decimal.new("-98765.43"), 7),
  decimal.to_packed(decimal.new("100200300.40"), 11),
  decimal.to_packed(decimal.new("-0.125"), 4),
}

local packed_digits = { 7, 7, 11, 4 }
local packed_scales = { 2, 2, 2, 3 }

local zoned_terms = {
  decimal.to_zoned(decimal.new("12345.67"), 7),
  decimal.to_zoned(decimal.new("-98765.43"), 7),
  decimal.to_zoned(decimal.new("100200300.40"), 11),
  decimal.to_zoned(decimal.new("-0.125"), 4),
}

local zoned_digits = { 7, 7, 11, 4 }
local zoned_scales = { 2, 2, 2, 3 }

local function parse_string_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local d = decimal.new(decimal_strings[((i - 1) % #decimal_strings) + 1])
    local s = decimal.tostring(d)
    total = total + #s + string.byte(s, #s)
  end
  return total
end

local function packed_roundtrip_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #packed_terms) + 1
    local d = decimal.from_packed(packed_terms[idx], packed_scales[idx])
    local out = decimal.to_packed(d)
    total = total + #out + string.byte(out, #out)
  end
  return total
end

local function packed_decode_object_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #packed_terms) + 1
    local s = decimal.tostring(decimal.from_packed(packed_terms[idx], packed_scales[idx]))
    total = total + #s + string.byte(s, #s)
  end
  return total
end

local function packed_encode_object_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #decimal_strings) + 1
    local out = decimal.to_packed(decimal.new(decimal_strings[idx]), packed_digits[idx])
    total = total + #out + string.byte(out, #out)
  end
  return total
end

local function packed_direct_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #packed_terms) + 1
    local s = decimal.packed_to_string(packed_terms[idx], packed_scales[idx])
    local out = decimal.string_to_packed(s, packed_digits[idx])
    total = total + #out + string.byte(out, #out) + #s
  end
  return total
end

local function packed_decode_direct_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #packed_terms) + 1
    local s = decimal.packed_to_string(packed_terms[idx], packed_scales[idx])
    total = total + #s + string.byte(s, #s)
  end
  return total
end

local function packed_encode_direct_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #decimal_strings) + 1
    local out = decimal.string_to_packed(decimal_strings[idx], packed_digits[idx])
    total = total + #out + string.byte(out, #out)
  end
  return total
end

local function packed_rescale_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #packed_terms) + 1
    local out = decimal.packed_rescale(packed_terms[idx], packed_scales[idx],
                                       packed_digits[idx])
    total = total + #out + string.byte(out, #out)
  end
  return total
end

local function zoned_roundtrip_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #zoned_terms) + 1
    local d = decimal.from_zoned(zoned_terms[idx], zoned_scales[idx])
    local out = decimal.to_zoned(d)
    total = total + #out + string.byte(out, #out)
  end
  return total
end

local function zoned_decode_object_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #zoned_terms) + 1
    local s = decimal.tostring(decimal.from_zoned(zoned_terms[idx], zoned_scales[idx]))
    total = total + #s + string.byte(s, #s)
  end
  return total
end

local function zoned_encode_object_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #decimal_strings) + 1
    local out = decimal.to_zoned(decimal.new(decimal_strings[idx]), zoned_digits[idx])
    total = total + #out + string.byte(out, #out)
  end
  return total
end

local function zoned_direct_loop(n)
  local total = 0
  local i
  for i = 1, n do
    local idx = ((i - 1) % #zoned_terms) + 1
    local s = decimal.zoned_to_string(zoned_terms[idx], zoned_scales[idx])
    local out = decimal.string_to_zoned(s, zoned_digits[idx])
    total = total + #out + string.byte(out, #out) + #s
  end
  return total
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local parse_expected = parse_string_loop(n)
  local packed_expected = packed_roundtrip_loop(n)
  local packed_decode_object_expected = packed_decode_object_loop(n)
  local packed_encode_object_expected = packed_encode_object_loop(n)
  local packed_direct_expected = packed_direct_loop(n)
  local packed_decode_direct_expected = packed_decode_direct_loop(n)
  local packed_encode_direct_expected = packed_encode_direct_loop(n)
  local packed_rescale_expected = packed_rescale_loop(n)
  local zoned_expected = zoned_roundtrip_loop(n)
  local zoned_decode_object_expected = zoned_decode_object_loop(n)
  local zoned_encode_object_expected = zoned_encode_object_loop(n)
  local zoned_direct_expected = zoned_direct_loop(n)
  cases[#cases + 1] = {
    workload = "parse_string",
    scale = scale,
    iterations = n,
    run = parse_string_loop,
    validate = function(result)
      bench.eq(result, parse_expected, "decimal_parse/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "packed_roundtrip",
    scale = scale,
    iterations = n,
    run = packed_roundtrip_loop,
    validate = function(result)
      bench.eq(result, packed_expected, "decimal_packed/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "packed_decode_object",
    scale = scale,
    iterations = n,
    run = packed_decode_object_loop,
    validate = function(result)
      bench.eq(result, packed_decode_object_expected,
	       "decimal_packed_decode_object/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "packed_encode_object",
    scale = scale,
    iterations = n,
    run = packed_encode_object_loop,
    validate = function(result)
      bench.eq(result, packed_encode_object_expected,
	       "decimal_packed_encode_object/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "packed_direct",
    scale = scale,
    iterations = n,
    run = packed_direct_loop,
    validate = function(result)
      bench.eq(result, packed_direct_expected, "decimal_packed_direct/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "packed_decode_direct",
    scale = scale,
    iterations = n,
    run = packed_decode_direct_loop,
    validate = function(result)
      bench.eq(result, packed_decode_direct_expected,
               "decimal_packed_decode_direct/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "packed_encode_direct",
    scale = scale,
    iterations = n,
    run = packed_encode_direct_loop,
    validate = function(result)
      bench.eq(result, packed_encode_direct_expected,
               "decimal_packed_encode_direct/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "packed_rescale",
    scale = scale,
    iterations = n,
    run = packed_rescale_loop,
    validate = function(result)
      bench.eq(result, packed_rescale_expected,
               "decimal_packed_rescale/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "zoned_roundtrip",
    scale = scale,
    iterations = n,
    run = zoned_roundtrip_loop,
    validate = function(result)
      bench.eq(result, zoned_expected, "decimal_zoned/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "zoned_decode_object",
    scale = scale,
    iterations = n,
    run = zoned_decode_object_loop,
    validate = function(result)
      bench.eq(result, zoned_decode_object_expected,
	       "decimal_zoned_decode_object/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "zoned_encode_object",
    scale = scale,
    iterations = n,
    run = zoned_encode_object_loop,
    validate = function(result)
      bench.eq(result, zoned_encode_object_expected,
	       "decimal_zoned_encode_object/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "zoned_direct",
    scale = scale,
    iterations = n,
    run = zoned_direct_loop,
    validate = function(result)
      bench.eq(result, zoned_direct_expected, "decimal_zoned_direct/" .. scale)
    end,
  }
end

bench.run_suite({ family = "decimal_convert", cases = cases })
