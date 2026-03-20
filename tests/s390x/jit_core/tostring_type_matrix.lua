local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local values = { "a", 2, function() end, 4 }
local total = 0

jit.opt.start("hotloop=2", "hotexit=2")

for rep = 1, 50 do
  for i = 1, #values do
    local s = tostring(values[i])
    if not s then
      error("missing tostring result")
    end
    total = total + #s
  end
end

t.assert(total > 0, "tostring matrix total")
print("tostring_type_matrix_total", total)
