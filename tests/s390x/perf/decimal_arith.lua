local bench = dofile("tests/s390x/perf/benchlib.lua")
local decimal = require("s390x.experimental.decimal")

local scales = {
  small = 2000,
  medium = 10000,
  hot = 40000,
}

local add_terms = {
  decimal.new("1.25"),
  decimal.new("2.50"),
  decimal.new("3.75"),
  decimal.new("4.00"),
}

local sub_terms = {
  decimal.new("1.10"),
  decimal.new("2.20"),
  decimal.new("3.30"),
  decimal.new("4.40"),
}

local function add_loop(n)
  local total = decimal.new("0")
  local i
  for i = 1, n do
    total = decimal.add(total, add_terms[((i - 1) % #add_terms) + 1])
  end
  return decimal.tostring(total)
end

local function sub_loop(n)
  local total = decimal.new("1000000.00")
  local i
  for i = 1, n do
    total = decimal.sub(total, sub_terms[((i - 1) % #sub_terms) + 1])
  end
  return decimal.tostring(total)
end

local function expected_add(n)
  local out = string.format("%.2f", (n / 4) * 11.5)
  out = out:gsub("0+$", "")
  out = out:gsub("%.$", "")
  return out
end

local function expected_sub(n)
  local out = string.format("%.2f", 1000000.0 - ((n / 4) * 11.0))
  out = out:gsub("0+$", "")
  out = out:gsub("%.$", "")
  return out
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local add_expected = expected_add(n)
  local sub_expected = expected_sub(n)
  cases[#cases + 1] = {
    workload = "add_loop",
    scale = scale,
    iterations = n,
    run = add_loop,
    validate = function(result)
      bench.eq(result, add_expected, "decimal_add/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "sub_loop",
    scale = scale,
    iterations = n,
    run = sub_loop,
    validate = function(result)
      bench.eq(result, sub_expected, "decimal_sub/" .. scale)
    end,
  }
end

bench.run_suite({ family = "decimal_arith", cases = cases })
