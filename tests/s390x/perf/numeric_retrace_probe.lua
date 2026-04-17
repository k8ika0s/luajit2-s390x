local jit = require("jit")
local util = require("jit.util")

local case_filter = os.getenv("S390X_RETRACE_CASE") or "all"
local n = tonumber(os.getenv("S390X_RETRACE_N") or "") or 512
local calls = tonumber(os.getenv("S390X_RETRACE_CALLS") or "") or 200
local trace_scan_limit = tonumber(os.getenv("S390X_RETRACE_TRACE_LIMIT") or "") or 4096
local attach_counters = os.getenv("S390X_RETRACE_ATTACH") ~= "0"

jit.opt.start("hotloop=1", "hotexit=1")

local function expected_abs(limit)
  return limit * (limit + 1) / 2
end

local function expected_min(limit)
  local half = math.floor(limit / 2)
  local total = half * (half + 1)
  if limit % 2 == 1 then
    total = total + half + 1
  end
  return total
end

local function expected_max(limit)
  return limit * (limit + 1) - expected_min(limit)
end

local function run_add(limit)
  local total = 0
  for i = 1, limit do
    total = total + i
  end
  return total
end

local function run_abs(limit)
  local total = 0
  for i = 1, limit do
    local signed = (i % 2 == 0) and -i or i
    total = total + math.abs(signed)
  end
  return total
end

local function run_min(limit)
  local total = 0
  for i = 1, limit do
    total = total + math.min(i, limit + 1 - i)
  end
  return total
end

local function run_max(limit)
  local total = 0
  for i = 1, limit do
    total = total + math.max(i, limit + 1 - i)
  end
  return total
end

local function new_counts()
  return {
    abort = 0,
    flush = 0,
    start = 0,
    stop = 0,
    texit = 0,
    trace_events = 0,
  }
end

local function count_traces()
  local traces = 0
  local max_trace = 0
  for tr = 1, trace_scan_limit do
    if util.traceinfo(tr) then
      traces = traces + 1
      max_trace = tr
    end
  end
  return traces, max_trace
end

local function run_case(case)
  local counts = new_counts()
  local first_result
  local last_result
  local ok = true
  local err = nil

  jit.flush()
  collectgarbage()

  local function on_trace(ev)
    counts.trace_events = counts.trace_events + 1
    if counts[ev] ~= nil then
      counts[ev] = counts[ev] + 1
    end
  end
  local function on_texit()
    counts.texit = counts.texit + 1
  end

  if attach_counters then
    jit.attach(on_trace, "trace")
    jit.attach(on_texit, "texit")
  end

  local started = os.clock()
  ok, err = pcall(function()
    for _ = 1, calls do
      last_result = case.fn(n)
      if first_result == nil then
	first_result = last_result
      end
      if last_result ~= case.expected(n) then
	error(string.format("%s: expected %.17g, got %.17g",
			    case.name, case.expected(n), last_result))
      end
    end
  end)
  local elapsed = os.clock() - started

  if attach_counters then
    jit.attach(on_texit)
    jit.attach(on_trace)
  end

  local traces, max_trace = count_traces()
  io.stdout:write(string.format(
    "RETRACE name=%s status=%s n=%d calls=%d elapsed=%.6f first=%.17g last=%.17g traces=%d max_trace=%d starts=%d stops=%d aborts=%d texits=%d flushes=%d trace_events=%d\n",
    case.name, ok and "ok" or "error", n, calls, elapsed,
    first_result or 0, last_result or 0, traces, max_trace,
    counts.start, counts.stop, counts.abort, counts.texit, counts.flush,
    counts.trace_events))

  if not ok then
    error(err)
  end
end

local cases = {
  { name = "add", fn = run_add, expected = expected_abs },
  { name = "abs", fn = run_abs, expected = expected_abs },
  { name = "min", fn = run_min, expected = expected_min },
  { name = "max", fn = run_max, expected = expected_max },
}

for i = 1, #cases do
  local case = cases[i]
  if case_filter == "all" or case_filter == case.name then
    run_case(case)
  end
end
