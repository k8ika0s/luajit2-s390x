local M = {}

local function append_package_path(prefix)
  if not package.path:find(prefix, 1, true) then
    package.path = prefix .. ";" .. package.path
  end
end

function M.enable_repo_jit_modules()
  append_package_path("./src/?.lua")
  append_package_path("./src/?/init.lua")
  append_package_path("./?.lua")
  append_package_path("./?/init.lua")
end

local function make_capture(kind)
  local jit = require("jit")
  local events = {}
  local active = true
  local function handler(...)
    if not active then
      return
    end
    local event = { n = select("#", ...) }
    for i = 1, event.n do
      event[i] = select(i, ...)
    end
    events[#events + 1] = event
  end
  jit.attach(handler, kind)
  return {
    events = events,
    kind = kind,
    stop = function()
      if active then
        active = false
        jit.attach(handler)
      end
    end,
  }
end

local function make_count_capture(kind, handler_factory)
  local jit = require("jit")
  local active = true
  local cap = handler_factory()
  local function handler(...)
    if not active then
      return
    end
    cap:record(...)
  end
  jit.attach(handler, kind)
  cap.stop = function()
    if active then
      active = false
      jit.attach(handler)
    end
  end
  return cap
end

function M.trace_capture()
  return make_capture("trace")
end

function M.texit_capture()
  return make_capture("texit")
end

function M.trace_counter_capture()
  return make_count_capture("trace", function()
    local cap = {
      total = 0,
      start = 0,
      stop_count = 0,
      abort = 0,
      hist = {},
    }
    function cap:record(kind, traceno)
      self.total = self.total + 1
      local kind_str = tostring(kind)
      local tr = tonumber(traceno)
      if kind_str == "start" then
        self.start = self.start + 1
      elseif kind_str == "stop" then
        self.stop_count = self.stop_count + 1
      elseif kind_str == "abort" then
        self.abort = self.abort + 1
      end
      if tr then
        local key = kind_str .. ":" .. tr
        self.hist[key] = (self.hist[key] or 0) + 1
      end
    end
    return cap
  end)
end

function M.trace_counter_capture_lite()
  return make_count_capture("trace", function()
    local cap = {
      total = 0,
      start = 0,
      stop_count = 0,
      abort = 0,
    }
    function cap:record(kind)
      self.total = self.total + 1
      local kind_str = tostring(kind)
      if kind_str == "start" then
        self.start = self.start + 1
      elseif kind_str == "stop" then
        self.stop_count = self.stop_count + 1
      elseif kind_str == "abort" then
        self.abort = self.abort + 1
      end
    end
    return cap
  end)
end

function M.texit_counter_capture()
  return make_count_capture("texit", function()
    local cap = {
      total = 0,
      hist = {},
    }
    function cap:record(traceno, exitno)
      local tr = tonumber(traceno)
      local ex = tonumber(exitno)
      self.total = self.total + 1
      if tr and ex then
        local key = tr .. ":" .. ex
        self.hist[key] = (self.hist[key] or 0) + 1
      end
    end
    return cap
  end)
end

function M.texit_counter_capture_lite()
  return make_count_capture("texit", function()
    local cap = {
      total = 0,
    }
    function cap:record()
      self.total = self.total + 1
    end
    return cap
  end)
end

function M.find_trace_event(events, kind)
  for i = 1, #events do
    if events[i][1] == kind then
      return events[i]
    end
  end
  return nil
end

function M.count_trace_events(events, kind)
  local count = 0
  for i = 1, #events do
    if events[i][1] == kind then
      count = count + 1
    end
  end
  return count
end

function M.assert_trace_stop(events, label)
  local ev = M.find_trace_event(events, "stop")
  M.truthy(ev, label or "trace stop")
  return ev
end

function M.traceinfo_snapshot(limit)
  local util = require("jit.util")
  local traces = {}
  if type(limit) == "number" and limit > 0 and limit < 256 then
    local info = util.traceinfo(limit)
    if info then
      traces[1] = {
        traceno = limit,
        link = tonumber(info.link) or 0,
        linktype = tostring(info.linktype),
        nins = tonumber(info.nins) or 0,
        nk = tonumber(info.nk) or 0,
        nexit = tonumber(info.nexit) or 0,
      }
    end
    return traces
  end
  limit = limit or 256
  for tr = 1, limit do
    local info = util.traceinfo(tr)
    if info then
      traces[#traces + 1] = {
        traceno = tr,
        link = tonumber(info.link) or 0,
        linktype = tostring(info.linktype),
        nins = tonumber(info.nins) or 0,
        nk = tonumber(info.nk) or 0,
        nexit = tonumber(info.nexit) or 0,
      }
    end
  end
  return traces
end

function M.with_finally(finalizer, fn)
  local ok, a, b, c, d = xpcall(fn, debug.traceback)
  pcall(finalizer)
  if not ok then
    error(a, 0)
  end
  return a, b, c, d
end

function M.eq(actual, expected, label)
  if actual ~= expected then
    error(string.format("%s: expected %s, got %s", label or "eq", tostring(expected), tostring(actual)), 2)
  end
end

function M.approx(actual, expected, epsilon, label)
  epsilon = epsilon or 1e-9
  if math.abs(actual - expected) > epsilon then
    error(string.format("%s: expected %.17g, got %.17g", label or "approx", expected, actual), 2)
  end
end

function M.truthy(value, label)
  if not value then
    error(string.format("%s: expected truthy value", label or "truthy"), 2)
  end
end

function M.assert(value, label)
  return M.truthy(value, label or "assert")
end

function M.same_array(actual, expected, label)
  M.eq(#actual, #expected, (label or "same_array") .. " length")
  for i = 1, #expected do
    if actual[i] ~= expected[i] then
      error(
        string.format(
          "%s: mismatch at index %d, expected %s, got %s",
          label or "same_array",
          i,
          tostring(expected[i]),
          tostring(actual[i])
        ),
        2
      )
    end
  end
end

return M
