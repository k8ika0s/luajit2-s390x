local jit = require("jit")
local util = require("jit.util")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2", "minstitch=1")

local events = {}

local function on_trace(what, tr, func, pc, otr, oex)
  events[#events + 1] = {
    kind = "trace",
    what = what,
    tr = tr,
    otr = otr,
    oex = oex,
    func = func,
    pc = pc,
  }
end

local function on_texit(tr, ex)
  events[#events + 1] = {
    kind = "texit",
    tr = tr,
    ex = ex,
  }
end

jit.attach(on_trace, "trace")
jit.attach(on_texit, "texit")

local tab = {}
for i = 1, 100 do
  local s = "a" .. i
  tab[s] = i
end

jit.attach(on_texit)
jit.attach(on_trace)
jit.off(true, true)

for i = 1, #events do
  local ev = events[i]
  if ev.kind == "trace" then
    print(string.format(
      "trace-event %d what=%s tr=%d otr=%s oex=%s",
      i,
      tostring(ev.what),
      tonumber(ev.tr) or -1,
      tostring(ev.otr),
      tostring(ev.oex)
    ))
  else
    print(string.format(
      "texit-event %d tr=%d ex=%d",
      i,
      tonumber(ev.tr) or -1,
      tonumber(ev.ex) or -1
    ))
  end
end

for tr = 1, 120 do
  local info = util.traceinfo(tr)
  if not info then
    break
  end
  print(string.format(
    "traceinfo tr=%d link=%d type=%s nins=%d nk=%d",
    tr,
    tonumber(info.link) or -1,
    tostring(info.linktype),
    tonumber(info.nins) or -1,
    tonumber(info.nk) or -1
  ))
end

print("done", tab.a1, tab.a100)
