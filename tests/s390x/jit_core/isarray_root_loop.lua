local jit = require("jit")
local jutil = require("jit.util")
local isarray = require("table.isarray")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=3", "hotexit=1")

local function on_trace(what, tr)
  if what ~= "stop" then
    return
  end
  local _, addr, loop = jutil.tracemc(tr)
  if addr then
    io.stderr:write(string.format("TRACEADDR tr=%d addr=0x%x loop=%d\n", tr, addr, loop or -1))
    io.stderr:flush()
  end
end

jit.attach(on_trace, "trace")

local value
local tab = { ["1"] = 3, ["2"] = 4 }
for _ = 1, 150 do
  value = isarray(tab)
end

jit.attach(on_trace)

t.eq(type(value), "boolean", "isarray type")
t.eq(value, false, "isarray result")
print("isarray", tostring(value))
