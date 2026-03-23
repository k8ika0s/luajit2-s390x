local jit = require("jit")
local jutil = require("jit.util")

local _M = {}

local state = {
  policy_calls_jit = 0,
  policy_calls_interp = 0,
}

function _M.init()
  state.policy_calls_jit = 0
  state.policy_calls_interp = 0
end

function _M.start_trace_listener()
  return nil
end

function _M.bump_policy_call(mode)
  if mode == "interp" then
    state.policy_calls_interp = state.policy_calls_interp + 1
  else
    state.policy_calls_jit = state.policy_calls_jit + 1
  end
end

function _M.log_first_trace_stop()
  return nil
end

function _M.snapshot()
  local enabled, flags = jit.status()
  local trace_count = 0
  while jutil.traceinfo(trace_count + 1) do
    trace_count = trace_count + 1
  end
  return {
    jit_enabled = enabled and true or false,
    trace_count = trace_count,
    policy_calls_jit = state.policy_calls_jit,
    policy_calls_interp = state.policy_calls_interp,
    worker_pid = ngx.worker.pid(),
    arch = jit.arch or "unknown",
    os = jit.os or "unknown",
    flags = { flags },
    note = "trace attachment disabled in OpenResty demo because the current branch still crashes in trace-observer request paths on s390x; trace_count is derived from jit.util",
  }
end

function _M.respond()
  ngx.header.content_type = "application/json"
  local snap = _M.snapshot()
  ngx.say(string.format(
    '{"jit_enabled":%s,"trace_count":%d,"policy_calls_jit":%d,"policy_calls_interp":%d,"worker_pid":%d,"arch":"%s","os":"%s","flags":["%s"],"note":"%s"}',
    snap.jit_enabled and "true" or "false",
    snap.trace_count,
    snap.policy_calls_jit,
    snap.policy_calls_interp,
    snap.worker_pid,
    snap.arch,
    snap.os,
    snap.flags[1] or "",
    snap.note
  ))
end

return _M
