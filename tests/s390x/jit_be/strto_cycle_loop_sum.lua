local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=1", "hotexit=2")

local chunk = [[
local strto_values = { "1.25", "2.5", "3.75", "4.125" }
return function(n)
  local total = 0
  for i = 1, n do
    total = total + tonumber(strto_values[(i % #strto_values) + 1])
  end
  return total
end
]]

local function build()
  return assert(loadstring(chunk, "@be_helpers_strto"))()
end

local fn = build()

jit.flush()
fn(20)
fn(20)
fn(20)
t.approx(fn(64000), 186000, 1e-9, "strto folded cycle")

local _, values = debug.getupvalue(fn, 1)
values[2] = "10.5"
t.approx(fn(64000), 314000, 1e-9, "strto slot guard exits")
