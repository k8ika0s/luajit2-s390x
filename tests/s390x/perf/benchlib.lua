local testlib = dofile("tests/s390x/helpers/testlib.lua")

local M = {}

local function clone_array(values)
  local out = {}
  for i = 1, #values do
    out[i] = values[i]
  end
  return out
end

local function sorted_copy(values)
  local out = clone_array(values)
  table.sort(out)
  return out
end

local function median(values)
  local ordered = sorted_copy(values)
  local n = #ordered
  if n == 0 then
    return 0
  end
  local mid = math.floor(n / 2) + 1
  if n % 2 == 1 then
    return ordered[mid]
  end
  return (ordered[mid - 1] + ordered[mid]) / 2
end

local function percentile(values, pct)
  local ordered = sorted_copy(values)
  if #ordered == 0 then
    return 0
  end
  local idx = math.ceil(#ordered * pct)
  if idx < 1 then
    idx = 1
  end
  if idx > #ordered then
    idx = #ordered
  end
  return ordered[idx]
end

local function json_escape(value)
  return value
    :gsub("\\", "\\\\")
    :gsub("\"", "\\\"")
    :gsub("\n", "\\n")
    :gsub("\r", "\\r")
    :gsub("\t", "\\t")
end

local function is_array(tbl)
  local n = #tbl
  for key, _ in pairs(tbl) do
    if type(key) ~= "number" or key < 1 or key > n or key % 1 ~= 0 then
      return false
    end
  end
  return true
end

local function encode_json(value)
  local t = type(value)
  if t == "nil" then
    return "null"
  end
  if t == "boolean" then
    return value and "true" or "false"
  end
  if t == "number" then
    if value ~= value or value == math.huge or value == -math.huge then
      error("cannot encode non-finite number")
    end
    return string.format("%.17g", value)
  end
  if t == "string" then
    return "\"" .. json_escape(value) .. "\""
  end
  if t == "table" then
    local parts = {}
    if is_array(value) then
      for i = 1, #value do
        parts[#parts + 1] = encode_json(value[i])
      end
      return "[" .. table.concat(parts, ",") .. "]"
    end
    local keys = {}
    for key, _ in pairs(value) do
      keys[#keys + 1] = key
    end
    table.sort(keys, function(a, b)
      return tostring(a) < tostring(b)
    end)
    for i = 1, #keys do
      local key = keys[i]
      parts[#parts + 1] = encode_json(tostring(key)) .. ":" .. encode_json(value[key])
    end
    return "{" .. table.concat(parts, ",") .. "}"
  end
  error("unsupported JSON type: " .. t)
end

local function append_jsonl(record)
  local path = os.getenv("S390X_PERF_OUTPUT_JSONL")
  if not path or path == "" then
    return
  end
  local fh, err = io.open(path, "a")
  if not fh then
    error("unable to open perf output: " .. tostring(err))
  end
  fh:write(
    "{",
    "\"bench_file\":", encode_json(record.bench_file),
    ",\"correct\":", encode_json(record.correct),
    ",\"family\":", encode_json(record.family),
    ",\"iterations\":", encode_json(record.iterations),
    ",\"measured_samples\":", encode_json(record.measured_samples),
    ",\"median_runtime_sec\":", encode_json(record.median_runtime_sec),
    ",\"p95_runtime_sec\":", encode_json(record.p95_runtime_sec),
    ",\"result\":", encode_json(record.result),
    ",\"samples_sec\":", encode_json(record.samples_sec),
    ",\"scale\":", encode_json(record.scale),
    ",\"warmup_iterations\":", encode_json(record.warmup_iterations),
    ",\"warmup_runs\":", encode_json(record.warmup_runs),
    ",\"workload\":", encode_json(record.workload),
    "}\n"
  )
  fh:close()
end

do
  local ok_jit, jit = pcall(require, "jit")
  if ok_jit then
    jit.off(encode_json, true)
    jit.off(append_jsonl, true)
  end
end

function M.configure_jit()
  local ok_jit, jit = pcall(require, "jit")
  if not ok_jit then
    return
  end
  local enabled = select(1, jit.status())
  if not enabled then
    return
  end
  local ok_opt, opt = pcall(require, "jit.opt")
  if ok_opt and opt and opt.start then
    local hotloop_env = os.getenv("S390X_PERF_HOTLOOP")
    local hotexit_env = os.getenv("S390X_PERF_HOTEXIT")
    local opts = {}
    if hotloop_env and hotloop_env ~= "" then
      opts[#opts + 1] = "hotloop=" .. assert(tonumber(hotloop_env), "bad S390X_PERF_HOTLOOP")
    end
    if hotexit_env and hotexit_env ~= "" then
      opts[#opts + 1] = "hotexit=" .. assert(tonumber(hotexit_env), "bad S390X_PERF_HOTEXIT")
    end
    if #opts > 0 then
      opt.start(unpack(opts))
    end
  end
end

function M.run_suite(spec)
  M.configure_jit()
  local warmup_default = tonumber(os.getenv("S390X_PERF_WARMUP") or "") or 1
  local sample_count = tonumber(os.getenv("S390X_PERF_SAMPLES") or "") or 5
  local bench_file = os.getenv("S390X_PERF_BENCH_FILE") or ""
  for _, case in ipairs(spec.cases) do
    local run = assert(case.run, "benchmark case missing run()")
    local validate = assert(case.validate, "benchmark case missing validate()")
    local iterations = assert(case.iterations, "benchmark case missing iterations")
    local warmup_iterations = case.warmup_iterations or iterations
    local warmup_runs = case.warmup_runs or warmup_default
    if case.setup then
      case.setup()
    end
    local baseline = run(iterations)
    validate(baseline)
    for _ = 1, warmup_runs do
      run(warmup_iterations)
    end
    local samples = {}
    local final_result = baseline
    for _ = 1, sample_count do
      collectgarbage()
      local start_clock = os.clock()
      local result = run(iterations)
      local elapsed = os.clock() - start_clock
      validate(result)
      samples[#samples + 1] = elapsed
      final_result = result
    end
    local post_result = run(iterations)
    validate(post_result)
    append_jsonl(
      {
        bench_file = bench_file,
        family = spec.family,
        workload = case.workload,
        scale = case.scale,
        iterations = iterations,
        warmup_iterations = warmup_iterations,
        warmup_runs = warmup_runs,
        measured_samples = sample_count,
        median_runtime_sec = median(samples),
        p95_runtime_sec = percentile(samples, 0.95),
        samples_sec = samples,
        correct = true,
        result = tostring(final_result),
      }
    )
    io.stdout:write(
      string.format(
        "PERF %s/%s/%s median=%.6f p95=%.6f\n",
        spec.family,
        case.workload,
        case.scale,
        median(samples),
        percentile(samples, 0.95)
      )
    )
  end
end

do
  local ok_jit, jit = pcall(require, "jit")
  if ok_jit then
    jit.off(M.run_suite)
  end
end

function M.scale_order(scales)
  local order = {}
  local seen = {}
  -- Prefer the hottest policy case first so smaller scales do not perturb the
  -- main throughput read for the same process.
  local preferred = { "hot", "small", "medium" }
  for i = 1, #preferred do
    local scale = preferred[i]
    if scales[scale] ~= nil then
      order[#order + 1] = scale
      seen[scale] = true
    end
  end
  local extras = {}
  for scale, _ in pairs(scales) do
    if not seen[scale] then
      extras[#extras + 1] = scale
    end
  end
  table.sort(extras)
  for i = 1, #extras do
    order[#order + 1] = extras[i]
  end
  return order
end

M.eq = testlib.eq
M.truthy = testlib.truthy

return M
