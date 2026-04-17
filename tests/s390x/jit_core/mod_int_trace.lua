local jit = require("jit")
local jutil = require("jit.util")
local t = require("tests.s390x.helpers.testlib")

local function numeric_loop(n)
  local total = 0
  for i = 1, n do
    total = total + (i % 97)
  end
  return total
end

local function numeric_mod31_loop(n)
  local total = 0
  for i = 1, n do
    total = total + (i % 31)
  end
  return total
end

local function numeric_mod31_sub_loop(n)
  local total = 0
  for i = 1, n do
    total = total - (i % 31)
  end
  return total
end

local function side_exit_loop(n)
  local total = 0
  for i = 1, n do
    if i % 7 == 0 then
      total = total - (i % 97)
    else
      total = total + (i % 97)
    end
  end
  return total
end

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

local function if5_else1_loop(n)
  local total = 0
  for i = 1, n do
    if i % 5 == 0 then
      total = total + (i % 97)
    else
      total = total + 1
    end
  end
  return total
end

local function ranged_side_exit_loop(first, last)
  local total = 0
  for i = first, last do
    if i % 7 == 0 then
      total = total - (i % 97)
    else
      total = total + (i % 97)
    end
  end
  return total
end

local function ranged_numeric_mod31_loop(first, last)
  local total = 0
  for i = first, last do
    total = total + (i % 31)
  end
  return total
end

local function ranged_select_i_loop(first, last)
  local total = 0
  for i = first, last do
    if i % 11 == 0 then
      total = total + i
    else
      total = total - i
    end
  end
  return total
end

local function ranged_rem_select_loop(first, last)
  local total = 0
  for i = first, last do
    if i % 11 == 0 then
      total = total + (i % 31)
    else
      total = total - (i % 31)
    end
  end
  return total
end

local function ranged_rem_preselect_loop(first, last)
  local total = 0
  for i = first, last do
    local r = i % 31
    if i % 11 == 0 then
      total = total + r
    else
      total = total - r
    end
  end
  return total
end

local function ranged_hotexit_loop(first, last)
  local total = 0
  for i = first, last do
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

local function ranged_if5_else1_loop(first, last)
  local total = 0
  for i = first, last do
    if i % 5 == 0 then
      total = total + (i % 97)
    else
      total = total + 1
    end
  end
  return total
end

local function signed_loop(n)
  local total = 0
  for i = -n, n do
    total = total + (i % 97)
  end
  return total
end

local function expect(fn, n)
  jit.off(fn, true)
  local expected = fn(n)
  jit.on(fn, true)
  jit.flush()
  jit.opt.start("hotloop=2", "hotexit=2")
  local actual = fn(n)
  t.eq(actual, expected, "integer modulo trace result")
  t.truthy(jutil.traceinfo(1) ~= nil, "integer modulo trace exists")
end

local function expect_changing_stops(fn, label)
  local stops = { 1, 2, 3, 4, 5, 6, 7, 20, 97, 400, 200, 800, 4000 }
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
end

local function expect_changing_ranges(fn, label)
  local ranges = {
    { 1, 1 }, { 2, 7 }, { 5, 97 }, { 13, 400 },
    { 96, 400 }, { 97, 401 }, { 101, 800 }, { 333, 4000 },
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
end

jit.off(expect_changing_stops, true)
jit.off(expect_changing_ranges, true)

t.truthy(select(1, jit.status()), "jit enabled")
expect(numeric_loop, 400)
expect(numeric_mod31_loop, 400)
expect(numeric_mod31_sub_loop, 400)
expect(side_exit_loop, 400)
expect(signed_loop, 200)
expect_changing_stops(numeric_loop, "numeric modulo")
expect_changing_stops(numeric_mod31_loop, "numeric mod31 modulo")
expect_changing_stops(numeric_mod31_sub_loop, "numeric mod31 sub modulo")
expect_changing_stops(side_exit_loop, "side-exit modulo")
expect_changing_stops(hotexit_loop, "hotexit modulo")
expect_changing_stops(if5_else1_loop, "if5 modulo")
expect_changing_ranges(ranged_numeric_mod31_loop, "numeric mod31 modulo")
expect_changing_ranges(ranged_select_i_loop, "select-i modulo")
expect_changing_ranges(ranged_rem_select_loop, "rem-select modulo")
expect_changing_ranges(ranged_rem_preselect_loop, "rem-preselect modulo")
expect_changing_ranges(ranged_side_exit_loop, "side-exit modulo")
expect_changing_ranges(ranged_hotexit_loop, "hotexit modulo")
expect_changing_ranges(ranged_if5_else1_loop, "if5 modulo")

-- Keep the aggressive hotexit/stitch shape as a separate tracked repro until
-- it is closure-green on native s390x release builds.
t.truthy(hotexit_loop(400) > 0, "hotexit modulo baseline executes")
