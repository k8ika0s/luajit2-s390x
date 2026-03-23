local bit = require("bit")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local function run()
  local abs_total = 0
  local min_total = 0
  local max_total = 0
  local div_total = 0
  local sqrt_total = 0
  local tobit_total = 0

  for i = 1, 200 do
    local signed = (i % 2 == 0) and -i or i
    abs_total = abs_total + math.abs(signed)
    min_total = min_total + math.min(i, 201 - i)
    max_total = max_total + math.max(i, 201 - i)
    div_total = div_total + ((i + 0.5) / (i + 1.25))
    sqrt_total = sqrt_total + math.sqrt(i + 0.25)
    tobit_total = tobit_total + bit.tobit(i * 65539)
  end

  return {
    abs_total = abs_total,
    min_total = min_total,
    max_total = max_total,
    div_total = div_total,
    sqrt_total = sqrt_total,
    tobit_total = tobit_total,
  }
end

local expected = run()
t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")
local actual = run()

t.eq(actual.abs_total, expected.abs_total, "abs total")
t.eq(actual.min_total, expected.min_total, "min total")
t.eq(actual.max_total, expected.max_total, "max total")
t.approx(actual.div_total, expected.div_total, 1e-12, "div total")
t.approx(actual.sqrt_total, expected.sqrt_total, 1e-12, "sqrt total")
t.eq(actual.tobit_total, expected.tobit_total, "tobit total")
