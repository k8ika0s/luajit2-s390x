local bit = require("bit")
local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 4000,
  medium = 16000,
  hot = 64000,
}

local function number_helper_loop(n)
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + i * 65537)
  end
  return bit.tobit(total)
end

local function be_pack_loop(n)
  local total = 0
  for i = 1, n do
    local b1 = bit.band(bit.rshift(i, 24), 0xff)
    local b2 = bit.band(bit.rshift(i, 16), 0xff)
    local b3 = bit.band(bit.rshift(i, 8), 0xff)
    local b4 = bit.band(i, 0xff)
    total = bit.tobit(total + bit.lshift(b1, 24) + bit.lshift(b2, 16) + bit.lshift(b3, 8) + b4)
  end
  return bit.tobit(total)
end

local strto_values = { "1.25", "2.5", "3.75", "4.125" }

local function strto_loop(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(strto_values[(i % #strto_values) + 1])
  end
  return total
end

local function strto_loop_ref(n)
  local ok_jit, jit = pcall(require, "jit")
  local enabled = ok_jit and jit.status()
  if ok_jit then
    jit.off(strto_loop, true)
  end
  local result = strto_loop(n)
  if ok_jit and enabled then
    jit.on(strto_loop, true)
  end
  return result
end

local STRTO_LOOP_CHUNK = [[
local strto_values = { "1.25", "2.5", "3.75", "4.125" }
return function(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(strto_values[(i % #strto_values) + 1])
  end
  return total
end
]]

local function build_strto_loop()
  return assert(loadstring(STRTO_LOOP_CHUNK, "@be_helpers_strto"))()
end

local function run_fresh(build, iterations)
  local fn = build()
  return fn(iterations)
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local expected_helper = number_helper_loop(n)
  local expected_pack = be_pack_loop(n)
  local expected_strto = strto_loop_ref(n)
  cases[#cases + 1] = {
    workload = "number_helper_loop",
    scale = scale,
    iterations = n,
    run = number_helper_loop,
    validate = function(result)
      bench.eq(result, expected_helper, "number_helper_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "be_pack_loop",
    scale = scale,
    iterations = n,
    run = be_pack_loop,
    validate = function(result)
      bench.eq(result, expected_pack, "be_pack_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "strto_loop",
    scale = scale,
    iterations = n,
    run = function(iterations)
      return run_fresh(build_strto_loop, iterations)
    end,
    validate = function(result)
      if math.abs(result - expected_strto) > 1e-9 then
        error("strto_loop/" .. scale .. ": expected " .. tostring(expected_strto) ..
              ", got " .. tostring(result))
      end
    end,
  }
end

bench.run_suite({ family = "be_helpers", cases = cases })
