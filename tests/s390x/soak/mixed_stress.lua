local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

ffi.cdef("typedef struct soak_pair { int a; int b; } soak_pair;")

jit.opt.start("hotloop=2", "hotexit=2", "minstitch=1")
if not select(1, jit.status()) then
  error("jit disabled", 0)
end

local total = 0
for round = 1, 25 do
  local pair = ffi.new("soak_pair", { a = round, b = round * 2 })
  for i = 1, 200 do
    total = total + pair.a + pair.b + i
  end
  if round % 5 == 0 then
    jit.flush()
  end
end

t.eq(total, 697500, "mixed soak total")
