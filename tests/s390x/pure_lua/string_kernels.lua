local t = require("tests.s390x.helpers.testlib")

local find_cases = {
  { haystack = "zzneedlezz", start_pos = 3, stop_pos = 8 },
  { haystack = "needle--", start_pos = 1, stop_pos = 6 },
  { haystack = "--needle", start_pos = 3, stop_pos = 8 },
}

local transform_cases = {
  {
    input = "AbC123xyZ",
    lower = "abc123xyz",
    upper = "ABC123XYZ",
    reverse = "Zyx321CbA",
  },
  {
    input = "LuaJITs390X",
    lower = "luajits390x",
    upper = "LUAJITS390X",
    reverse = "X093sTIJauL",
  },
  {
    input = "MiXeD-Case-09",
    lower = "mixed-case-09",
    upper = "MIXED-CASE-09",
    reverse = "90-esaC-DeXiM",
  },
}

local compare_cases = {
  { left = "abc", right = "abd", lt = true, le = true },
  { left = "lua", right = "LUA", lt = false, le = false },
  { left = "same", right = "same", lt = false, le = true },
}

for i = 1, #find_cases do
  local case = find_cases[i]
  local a, b = string.find(case.haystack, "needle", 1, true)
  t.eq(a, case.start_pos, "find start " .. i)
  t.eq(b, case.stop_pos, "find stop " .. i)
end

for i = 1, #transform_cases do
  local case = transform_cases[i]
  t.eq(string.lower(case.input), case.lower, "lower " .. i)
  t.eq(string.upper(case.input), case.upper, "upper " .. i)
  t.eq(string.reverse(case.input), case.reverse, "reverse " .. i)
end

for i = 1, #compare_cases do
  local case = compare_cases[i]
  t.eq(case.left < case.right, case.lt, "lt " .. i)
  t.eq(case.left <= case.right, case.le, "le " .. i)
end

local function run(n)
  local total = 0
  for i = 1, n do
    local idx = ((i - 1) % 3) + 1
    local fcase = find_cases[idx]
    local tcase = transform_cases[idx]
    local ccase = compare_cases[idx]
    local a, b = string.find(fcase.haystack, "needle", 1, true)
    local lower = string.lower(tcase.input)
    local upper = string.upper(tcase.input)
    local reverse = string.reverse(tcase.input)
    total = total + a + b
    if ccase.left < ccase.right then total = total + 13 else total = total + 2 end
    if ccase.left <= ccase.right then total = total + 17 else total = total + 3 end
    if tcase.input < upper then total = total + 7 else total = total + 1 end
    if lower < tcase.input then total = total + 5 end
    total = total + string.byte(reverse, 1) + #lower + #upper
  end
  return total
end

t.eq(run(210), 27090, "string kernel total")
