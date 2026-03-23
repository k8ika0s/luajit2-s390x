local jit = require("jit")
local jutil = require("jit.util")

local _M = {}

local state = {
  policy_calls_jit = 0,
  policy_calls_interp = 0,
}

local function metrics_dict()
  return ngx.shared.jit_metrics
end

local function observer_enabled()
  return os.getenv("S390X_DEMO_TRACE_OBSERVER") == "1"
end

local function set_default(key, value)
  local dict = metrics_dict()
  if not dict then
    return
  end
  dict:safe_set(key, value)
end

local function incr(key, amount)
  local dict = metrics_dict()
  if not dict then
    return nil
  end
  local val, err = dict:incr(key, amount or 1)
  if not val and err == "not found" then
    dict:set(key, 0)
    val = dict:incr(key, amount or 1)
  end
  return val
end

local function trace_handler(what, tr, func, pc, otr, oex)
  incr("observer.trace_events", 1)
  if what == "start" then
    incr("observer.trace_start", 1)
  elseif what == "stop" then
    local count = incr("observer.trace_stop", 1) or 0
    if count == 1 then
      local info = jutil.traceinfo(tr)
      local dict = metrics_dict()
      if dict then
        dict:set("observer.first_stop_traceno", tonumber(tr) or 0)
        dict:set("observer.first_stop_link", info and tonumber(info.link) or 0)
        dict:set("observer.first_stop_linktype", info and tostring(info.linktype) or "unknown")
      end
    end
  elseif what == "abort" then
    incr("observer.trace_abort", 1)
  elseif what == "flush" then
    incr("observer.trace_flush", 1)
  end
end

local function texit_handler(tr, ex)
  incr("observer.texit", 1)
  local dict = metrics_dict()
  if dict then
    dict:set("observer.last_texit_trace", tonumber(tr) or 0)
    dict:set("observer.last_texit_exit", tonumber(ex) or 0)
  end
end

function _M.init()
  state.policy_calls_jit = 0
  state.policy_calls_interp = 0
  local dict = metrics_dict()
  if dict then
    dict:flush_all()
    dict:flush_expired()
    set_default("observer.enabled", observer_enabled() and 1 or 0)
    set_default("observer.trace_events", 0)
    set_default("observer.trace_start", 0)
    set_default("observer.trace_stop", 0)
    set_default("observer.trace_abort", 0)
    set_default("observer.trace_flush", 0)
    set_default("observer.texit", 0)
    set_default("observer.first_stop_traceno", 0)
    set_default("observer.first_stop_link", 0)
    set_default("observer.first_stop_linktype", "")
    set_default("observer.last_texit_trace", 0)
    set_default("observer.last_texit_exit", 0)
  end
end

function _M.start_trace_listener()
  if not observer_enabled() then
    return nil
  end
  local ok, err = pcall(function()
    jit.attach(trace_handler, "trace")
    jit.attach(texit_handler, "texit")
  end)
  local dict = metrics_dict()
  if dict then
    dict:set("observer.attach_ok", ok and 1 or 0)
    dict:set("observer.attach_error", ok and "" or tostring(err))
  end
  return ok, err
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
  local dict = metrics_dict()
  local enabled, flags = jit.status()
  local trace_count = 0
  while jutil.traceinfo(trace_count + 1) do
    trace_count = trace_count + 1
  end
  local observer_active = dict and dict:get("observer.enabled") == 1 or false
  local attach_ok = dict and dict:get("observer.attach_ok") == 1 or false
  local first_stop_linktype = dict and dict:get("observer.first_stop_linktype") or ""
  local note
  if observer_active and attach_ok then
    note = "trace observer enabled in worker; event counters come from jit.attach plus trace_count from jit.util"
  elseif observer_active then
    note = "trace observer requested but did not attach cleanly; trace_count falls back to jit.util-only counters"
  else
    note = "trace observer disabled for demo stability; trace_count is derived from jit.util"
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
    observer_enabled = observer_active,
    observer_attach_ok = attach_ok,
    trace_events = dict and (dict:get("observer.trace_events") or 0) or 0,
    trace_start = dict and (dict:get("observer.trace_start") or 0) or 0,
    trace_stop = dict and (dict:get("observer.trace_stop") or 0) or 0,
    trace_abort = dict and (dict:get("observer.trace_abort") or 0) or 0,
    trace_flush = dict and (dict:get("observer.trace_flush") or 0) or 0,
    texit_count = dict and (dict:get("observer.texit") or 0) or 0,
    first_stop_traceno = dict and (dict:get("observer.first_stop_traceno") or 0) or 0,
    first_stop_link = dict and (dict:get("observer.first_stop_link") or 0) or 0,
    first_stop_linktype = first_stop_linktype,
    last_texit_trace = dict and (dict:get("observer.last_texit_trace") or 0) or 0,
    last_texit_exit = dict and (dict:get("observer.last_texit_exit") or 0) or 0,
    observer_attach_error = dict and (dict:get("observer.attach_error") or "") or "",
    note = note,
  }
end

function _M.respond()
  ngx.header.content_type = "application/json"
  local snap = _M.snapshot()
  ngx.say(string.format(
    '{"jit_enabled":%s,"trace_count":%d,"policy_calls_jit":%d,"policy_calls_interp":%d,"worker_pid":%d,"arch":"%s","os":"%s","flags":["%s"],"observer_enabled":%s,"observer_attach_ok":%s,"trace_events":%d,"trace_start":%d,"trace_stop":%d,"trace_abort":%d,"trace_flush":%d,"texit_count":%d,"first_stop_traceno":%d,"first_stop_link":%d,"first_stop_linktype":"%s","last_texit_trace":%d,"last_texit_exit":%d,"observer_attach_error":"%s","note":"%s"}',
    snap.jit_enabled and "true" or "false",
    snap.trace_count,
    snap.policy_calls_jit,
    snap.policy_calls_interp,
    snap.worker_pid,
    snap.arch,
    snap.os,
    snap.flags[1] or "",
    snap.observer_enabled and "true" or "false",
    snap.observer_attach_ok and "true" or "false",
    snap.trace_events,
    snap.trace_start,
    snap.trace_stop,
    snap.trace_abort,
    snap.trace_flush,
    snap.texit_count,
    snap.first_stop_traceno,
    snap.first_stop_link,
    snap.first_stop_linktype,
    snap.last_texit_trace,
    snap.last_texit_exit,
    snap.observer_attach_error,
    snap.note
  ))
end

return _M
