local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 4000,
  medium = 16000,
  hot = 64000,
}

local function abs_loop(n)
  local total = 0
  for i = 1, n do
    local signed = (i % 2 == 0) and -i or i
    total = total + math.abs(signed)
  end
  return total
end

local function div_loop(n)
  local total = 0
  for i = 1, n do
    total = total + ((i + 0.5) / (i + 1.25))
  end
  return total
end

local function fp_mod_loop(n)
  local total = 0
  for i = 1, n do
    total = total + ((i + 0.25) % 7.5) + ((-i - 0.5) % 5.25)
  end
  return total
end

local function sqrt_loop(n)
  local total = 0
  for i = 1, n do
    total = total + math.sqrt(i + 0.25)
  end
  return total
end

local function min_loop(n)
  local total = 0
  for i = 1, n do
    total = total + math.min(i, n + 1 - i)
  end
  return total
end

local function max_loop(n)
  local total = 0
  for i = 1, n do
    total = total + math.max(i, n + 1 - i)
  end
  return total
end

local function make_reference(fn)
  local ref = fn
  local ok_jit, jit = pcall(require, "jit")
  if ok_jit and jit and jit.off then
    jit.off(ref, true)
  end
  return ref
end

local abs_loop_ref = make_reference(abs_loop)
local div_loop_ref = make_reference(div_loop)
local fp_mod_loop_ref = make_reference(fp_mod_loop)
local sqrt_loop_ref = make_reference(sqrt_loop)
local min_loop_ref = make_reference(min_loop)
local max_loop_ref = make_reference(max_loop)

local function run_fresh(builder, n)
  return builder()(n)
end

local ABS_LOOP_CHUNK = [[
return function(n)
  local total = 0
  for i = 1, n do
    local signed = (i % 2 == 0) and -i or i
    total = total + math.abs(signed)
  end
  return total
end
]]

local DIV_LOOP_CHUNK = [[
return function(n)
  local total = 0
  for i = 1, n do
    total = total + ((i + 0.5) / (i + 1.25))
  end
  return total
end
]]

local FP_MOD_LOOP_CHUNK = [[
return function(n)
  local total = 0
  for i = 1, n do
    total = total + ((i + 0.25) % 7.5) + ((-i - 0.5) % 5.25)
  end
  return total
end
]]

local SQRT_LOOP_CHUNK = [[
return function(n)
  local total = 0
  for i = 1, n do
    total = total + math.sqrt(i + 0.25)
  end
  return total
end
]]

local MIN_LOOP_CHUNK = [[
return function(n)
  local total = 0
  for i = 1, n do
    total = total + math.min(i, n + 1 - i)
  end
  return total
end
]]

local MAX_LOOP_CHUNK = [[
return function(n)
  local total = 0
  for i = 1, n do
    total = total + math.max(i, n + 1 - i)
  end
  return total
end
]]

local function load_loop(chunk, name)
  local loader = assert(loadstring(chunk, name))
  return loader()
end

local function build_abs_loop()
  return load_loop(ABS_LOOP_CHUNK, "@numeric_ops_abs")
end

local function build_div_loop()
  return load_loop(DIV_LOOP_CHUNK, "@numeric_ops_div")
end

local function build_fp_mod_loop()
  return load_loop(FP_MOD_LOOP_CHUNK, "@numeric_ops_fp_mod")
end

local function build_sqrt_loop()
  return load_loop(SQRT_LOOP_CHUNK, "@numeric_ops_sqrt")
end

local function build_min_loop()
  return load_loop(MIN_LOOP_CHUNK, "@numeric_ops_min")
end

local function build_max_loop()
  return load_loop(MAX_LOOP_CHUNK, "@numeric_ops_max")
end

local function approx_eq(actual, expected, epsilon, label)
  if math.abs(actual - expected) > epsilon then
    error(string.format("%s: expected %.17g, got %.17g", label, expected, actual))
  end
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local expected_abs = abs_loop_ref(n)
  local expected_div = div_loop_ref(n)
  local expected_fp_mod = fp_mod_loop_ref(n)
  local expected_sqrt = sqrt_loop_ref(n)
  local expected_min = min_loop_ref(n)
  local expected_max = max_loop_ref(n)
  cases[#cases + 1] = {
    workload = "abs_loop",
    scale = scale,
    iterations = n,
    run = function(iterations)
      return run_fresh(build_abs_loop, iterations)
    end,
    validate = function(result)
      bench.eq(result, expected_abs, "abs_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "div_loop",
    scale = scale,
    iterations = n,
    run = function(iterations)
      return run_fresh(build_div_loop, iterations)
    end,
    validate = function(result)
      approx_eq(result, expected_div, 1e-12, "div_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "fp_mod_loop",
    scale = scale,
    iterations = n,
    run = function(iterations)
      return run_fresh(build_fp_mod_loop, iterations)
    end,
    validate = function(result)
      approx_eq(result, expected_fp_mod, 1e-9, "fp_mod_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "sqrt_loop",
    scale = scale,
    iterations = n,
    run = function(iterations)
      return run_fresh(build_sqrt_loop, iterations)
    end,
    validate = function(result)
      approx_eq(result, expected_sqrt, 1e-12, "sqrt_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "min_loop",
    scale = scale,
    iterations = n,
    run = function(iterations)
      return run_fresh(build_min_loop, iterations)
    end,
    validate = function(result)
      bench.eq(result, expected_min, "min_loop/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "max_loop",
    scale = scale,
    iterations = n,
    run = function(iterations)
      return run_fresh(build_max_loop, iterations)
    end,
    validate = function(result)
      bench.eq(result, expected_max, "max_loop/" .. scale)
    end,
  }
end

bench.run_suite({ family = "numeric_ops", cases = cases })
