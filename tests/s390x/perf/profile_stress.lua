local bench = dofile("tests/s390x/perf/benchlib.lua")
local profile = require("jit.profile")

local scales = {
  small = 1200000,
  medium = 1800000,
  hot = 2700000,
}

local function work_range(first, last, total)
  total = total or 0.0
  for i = first, last do
    local v = i % 97
    if i % 11 == 0 then
      total = total + (v * 3)
    elseif i % 7 == 0 then
      total = total - v
    else
      total = total + v
    end
  end
  return total
end

local function work(limit)
  return work_range(1, limit, 0.0)
end

local function run_control(limit)
  return {
    total = work(limit),
    callbacks = 0,
    samples = 0,
    chunks = 0,
  }
end

local function run_profile_active(limit, mode)
  local callbacks = 0
  local samples = 0
  local total
  profile.start(mode, function(_, count)
    callbacks = callbacks + 1
    samples = samples + count
  end)
  total = work(limit)
  profile.stop()
  return {
    total = total,
    callbacks = callbacks,
    samples = samples,
    chunks = 1,
  }
end

local function run_profile_toggle(limit, mode)
  local callbacks = 0
  local samples = 0
  local total = 0.0
  local chunks = 6
  local chunk_size = math.floor(limit / chunks)
  local first = 1
  for chunk = 1, chunks do
    local last = first + chunk_size - 1
    if chunk == chunks then
      last = limit
    end
    profile.start(mode, function(_, count)
      callbacks = callbacks + 1
      samples = samples + count
    end)
    total = work_range(first, last, total)
    profile.stop()
    first = last + 1
  end
  return {
    total = total,
    callbacks = callbacks,
    samples = samples,
    chunks = chunks,
  }
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local expected = work(n)
  local function validate(label, result, require_samples)
    bench.approx(result.total, expected, 1e-9, label .. "/" .. scale .. "/total")
    if require_samples then
      bench.truthy(result.callbacks > 0, label .. "/" .. scale .. "/callbacks")
      bench.truthy(result.samples >= result.callbacks, label .. "/" .. scale .. "/samples")
    end
  end
  cases[#cases + 1] = {
    workload = "control_loop",
    scale = scale,
    iterations = n,
    warmup_runs = 0,
    run = run_control,
    validate = function(result)
      validate("control_loop", result, false)
    end,
  }
  cases[#cases + 1] = {
    workload = "sample_active",
    scale = scale,
    iterations = n,
    warmup_runs = 0,
    run = function(limit)
      return run_profile_active(limit, "i1")
    end,
    validate = function(result)
      validate("sample_active", result, true)
    end,
  }
  cases[#cases + 1] = {
    workload = "sample_toggle",
    scale = scale,
    iterations = n,
    warmup_runs = 0,
    run = function(limit)
      return run_profile_toggle(limit, "i1")
    end,
    validate = function(result)
      validate("sample_toggle", result, true)
    end,
  }
  cases[#cases + 1] = {
    workload = "trace_active",
    scale = scale,
    iterations = n,
    warmup_runs = 0,
    run = function(limit)
      return run_profile_active(limit, "fi1")
    end,
    validate = function(result)
      validate("trace_active", result, true)
    end,
  }
  cases[#cases + 1] = {
    workload = "trace_toggle",
    scale = scale,
    iterations = n,
    warmup_runs = 0,
    run = function(limit)
      return run_profile_toggle(limit, "fi1")
    end,
    validate = function(result)
      validate("trace_toggle", result, true)
    end,
  }
end

bench.run_suite({ family = "profile_stress", cases = cases })
