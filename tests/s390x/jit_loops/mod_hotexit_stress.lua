local jit = require("jit")
local jutil = require("jit.util")
local t = require("tests.s390x.helpers.testlib")

local function hotexit_loop(n)
  local total = 0
  for i = 1, n do
    if i % 5 == 0 then
      total = total + ((i % 97) * 3)
    elseif i % 3 == 0 then
      total = total - (i % 97)
    else
      total = total + 1
    end
  end
  return total
end

jit.flush()
jit.opt.start("hotloop=2", "hotexit=2")

local expected = hotexit_loop(400)
local actual = hotexit_loop(400)

t.eq(actual, expected, "hotexit modulo stress result")
t.truthy(jutil.traceinfo(1) ~= nil, "hotexit modulo root trace exists")
