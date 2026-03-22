local jit = require("jit")

jit.opt.start("hotloop=1")

local function retlast(...)
  return select(select("#", ...), ...)
end

for i = 1, 2 do
  local a = 1
  local b = 2
  local c = 3
  local d = retlast(a, b, c, i)
  print("SEQ", i, a * 1000 + b * 100 + c * 10 + d)
end
