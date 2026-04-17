local jit = require("jit")
local jutil = require("jit.util")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=1")

local function side_exit_mul3_loop(n)
  local total = 0
  for i = 1, n do
    local value = i
    if i % 7 == 0 then
      value = value * 3
    end
    total = total + value
  end
  return total
end

local function side_exit_mul4_mod5_loop(n)
  local total = 0
  for i = 1, n do
    local value = i
    if i % 5 == 0 then
      value = value * 4
    end
    total = total + value
  end
  return total
end

local function direct_mul3_loop(n)
  local total = 0
  for i = 1, n do
    if i % 7 == 0 then
      total = total + i * 3
    else
      total = total + i
    end
  end
  return total
end

local function nonzero_guard_loop(n)
  local total = 0
  for i = 1, n do
    if i % 7 == 3 then
      total = total + i * 2
    else
      total = total + i
    end
  end
  return total
end

local function nonzero_ne_guard_loop(n)
  local total = 0
  for i = 1, n do
    if i % 13 ~= 5 then
      total = total + i
    else
      total = total - i * 2
    end
  end
  return total
end

local function signed_nonzero_lt_guard_loop(n)
  local total = 0
  for i = 1, n do
    local value = (i % 17) - 8
    if value < -3 then
      total = total + i * 3
    else
      total = total - value
    end
  end
  return total
end

local function direct_addsub_loop(n)
  local total = 0
  for i = 1, n do
    if i % 7 == 0 then
      total = total - i
    else
      total = total + i
    end
  end
  return total
end

local function mod11_mod31_addsub_loop(n)
  local total = 0
  for i = 1, n do
    if i % 11 == 0 then
      total = total - (i % 31)
    else
      total = total + (i % 31)
    end
  end
  return total
end

local function mod9_mod41_subadd_loop(n)
  local total = 0
  for i = 1, n do
    if i % 9 == 0 then
      total = total + (i % 41)
    else
      total = total - (i % 41)
    end
  end
  return total
end

local function preselect_mod11_mod31_addsub_loop(n)
  local total = 0
  for i = 1, n do
    local r = i % 31
    if i % 11 == 0 then
      total = total - r
    else
      total = total + r
    end
  end
  return total
end

local function ranged_side_exit_mul3_loop(first, last)
  local total = 0
  for i = first, last do
    local value = i
    if i % 7 == 0 then
      value = value * 3
    end
    total = total + value
  end
  return total
end

local function ranged_side_exit_mul4_mod5_loop(first, last)
  local total = 0
  for i = first, last do
    local value = i
    if i % 5 == 0 then
      value = value * 4
    end
    total = total + value
  end
  return total
end

local function ranged_direct_mul3_loop(first, last)
  local total = 0
  for i = first, last do
    if i % 7 == 0 then
      total = total + i * 3
    else
      total = total + i
    end
  end
  return total
end

local function ranged_nonzero_guard_loop(first, last)
  local total = 0
  for i = first, last do
    if i % 7 == 3 then
      total = total + i * 2
    else
      total = total + i
    end
  end
  return total
end

local function ranged_nonzero_ne_guard_loop(first, last)
  local total = 0
  for i = first, last do
    if i % 13 ~= 5 then
      total = total + i
    else
      total = total - i * 2
    end
  end
  return total
end

local function ranged_signed_nonzero_lt_guard_loop(first, last)
  local total = 0
  for i = first, last do
    local value = (i % 17) - 8
    if value < -3 then
      total = total + i * 3
    else
      total = total - value
    end
  end
  return total
end

local function ranged_direct_addsub_loop(first, last)
  local total = 0
  for i = first, last do
    if i % 7 == 0 then
      total = total - i
    else
      total = total + i
    end
  end
  return total
end

local function ranged_mod11_mod31_addsub_loop(first, last)
  local total = 0
  for i = first, last do
    if i % 11 == 0 then
      total = total - (i % 31)
    else
      total = total + (i % 31)
    end
  end
  return total
end

local function ranged_preselect_mod11_mod31_addsub_loop(first, last)
  local total = 0
  for i = first, last do
    local r = i % 31
    if i % 11 == 0 then
      total = total - r
    else
      total = total + r
    end
  end
  return total
end

local function expect_changing_stops(fn, label)
  local stops = { 1, 2, 6, 7, 8, 20, 97, 200, 53, 400, 1000 }
  local expected = {}
  jit.off(fn, true)
  for i = 1, #stops do
    expected[i] = fn(stops[i])
  end
  jit.on(fn, true)
  jit.flush()
  jit.opt.start("hotloop=1", "hotexit=1")
  for pass = 1, 3 do
    for i = 1, #stops do
      local n = stops[i]
      t.eq(fn(n), expected[i], label .. " changing stop " .. n .. " pass " .. pass)
    end
  end
  t.truthy(jutil.traceinfo(1) ~= nil, label .. " trace exists")
end

local function expect_changing_ranges(fn, label)
  local ranges = {
    { 1, 1 }, { 2, 7 }, { 5, 97 }, { 13, 200 },
    { 96, 400 }, { 97, 401 }, { 101, 800 },
  }
  local expected = {}
  jit.off(fn, true)
  for i = 1, #ranges do
    expected[i] = fn(ranges[i][1], ranges[i][2])
  end
  jit.on(fn, true)
  jit.flush()
  jit.opt.start("hotloop=1", "hotexit=1")
  for pass = 1, 3 do
    for i = 1, #ranges do
      local first = ranges[i][1]
      local last = ranges[i][2]
      t.eq(fn(first, last), expected[i],
           label .. " changing range " .. first .. ".." .. last ..
           " pass " .. pass)
    end
  end
  t.truthy(jutil.traceinfo(1) ~= nil, label .. " trace exists")
end

jit.off(expect_changing_stops, true)
jit.off(expect_changing_ranges, true)

local total = 0
for i = 1, 200 do
  local value = i
  if i % 7 == 0 then
    value = value * 3
  end
  total = total + value
end

t.eq(total, 25784, "side exit total")
expect_changing_stops(side_exit_mul3_loop, "side exit mul3")
expect_changing_stops(side_exit_mul4_mod5_loop, "side exit mul4 mod5")
expect_changing_stops(direct_mul3_loop, "direct mul3")
expect_changing_stops(nonzero_guard_loop, "nonzero guard")
expect_changing_stops(nonzero_ne_guard_loop, "nonzero ne guard")
expect_changing_stops(signed_nonzero_lt_guard_loop, "signed nonzero lt guard")
expect_changing_stops(direct_addsub_loop, "direct addsub")
expect_changing_stops(mod11_mod31_addsub_loop, "mod11 mod31 addsub")
expect_changing_stops(mod9_mod41_subadd_loop, "mod9 mod41 subadd")
expect_changing_stops(preselect_mod11_mod31_addsub_loop, "preselect mod11 mod31 addsub")
expect_changing_ranges(ranged_side_exit_mul3_loop, "side exit mul3")
expect_changing_ranges(ranged_side_exit_mul4_mod5_loop, "side exit mul4 mod5")
expect_changing_ranges(ranged_direct_mul3_loop, "direct mul3")
expect_changing_ranges(ranged_nonzero_guard_loop, "nonzero guard")
expect_changing_ranges(ranged_nonzero_ne_guard_loop, "nonzero ne guard")
expect_changing_ranges(ranged_signed_nonzero_lt_guard_loop,
                       "signed nonzero lt guard")
expect_changing_ranges(ranged_direct_addsub_loop, "direct addsub")
expect_changing_ranges(ranged_mod11_mod31_addsub_loop, "mod11 mod31 addsub")
expect_changing_ranges(ranged_preselect_mod11_mod31_addsub_loop,
                       "preselect mod11 mod31 addsub")
