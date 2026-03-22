local jit = require("jit")

jit.opt.start("hotloop=1")

local function retconst(...)
  return 42
end

local function retlast(...)
  return select(select("#", ...), ...)
end

for i = 1, 2 do
  print("CONST", i, retconst(1, 2, 3, i))
end

for i = 1, 2 do
  print("LAST", i, retlast(1, 2, 3, i))
end
