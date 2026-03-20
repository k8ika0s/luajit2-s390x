local jit = require("jit")
local util = require("jit.util")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=1", "minstitch=1")

local current_iter = 0

local function on_trace(what, tr, func, pc, otr, oex)
  print(string.format(
    "TRACE_%s iter=%d tr=%s otr=%s oex=%s",
    tostring(what),
    current_iter,
    tostring(tr),
    tostring(otr),
    tostring(oex)
  ))
end

jit.attach(on_trace, "trace")

local tab = {}
for i = 1, 100 do
  current_iter = i
  local s = "a" .. ((i - 1) % 20 + 1)
  tab[s] = i
end

jit.attach(on_trace)
jit.off(true, true)

for tr = 1, 64 do
  local info = util.traceinfo(tr)
  if not info then
    break
  end
  print(string.format(
    "TRACEINFO tr=%d link=%s type=%s nins=%s nk=%s",
    tr,
    tostring(info.link),
    tostring(info.linktype),
    tostring(info.nins),
    tostring(info.nk)
  ))
end

print("done", tab.a1, tab.a20)
