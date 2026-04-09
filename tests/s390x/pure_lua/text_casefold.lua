local t = require("tests.s390x.helpers.testlib")

local cases = {
  {
    input = "LuaJIT-S390x-AbCdEfGh",
    lower = "luajit-s390x-abcdefgh",
    upper = "LUAJIT-S390X-ABCDEFGH",
  },
  {
    input = "Already-lower-0123",
    lower = "already-lower-0123",
    upper = "ALREADY-LOWER-0123",
  },
  {
    input = "MIXED_case-Longer-Value-XYZxyz",
    lower = "mixed_case-longer-value-xyzxyz",
    upper = "MIXED_CASE-LONGER-VALUE-XYZXYZ",
  },
}

for i = 1, #cases do
  local case = cases[i]
  t.eq(string.lower(case.input), case.lower, "casefold lower " .. i)
  t.eq(string.upper(case.input), case.upper, "casefold upper " .. i)
end

local function run(n)
  local total = 0
  for i = 1, n do
    local case = cases[((i - 1) % #cases) + 1]
    local lower = string.lower(case.input)
    local upper = string.upper(case.input)
    total = total + string.byte(lower, 1) + string.byte(upper, #upper) + #lower + #upper
  end
  return total
end

t.eq(run(240), 53200, "casefold total")
