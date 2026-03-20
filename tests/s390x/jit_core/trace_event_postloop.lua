local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

ffi.cdef("int abs(int x);")

local cabs = ffi.C.abs
local cap = t.trace_capture()
local total = 0

jit.opt.start("hotloop=2", "hotexit=2")

for i = 1, 50 do
  total = total + cabs((i % 11) - 5)
end

cap.stop()

t.eq(total, 131, "trace event postloop total")

for i, ev in ipairs(cap.events) do
  print("event", i, "n", ev.n)
  for j = 1, ev.n do
    print("idx", j, tostring(ev[j]))
  end
end
