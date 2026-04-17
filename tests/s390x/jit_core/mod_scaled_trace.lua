local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

local function mod31_scaled_loop(n)
  local total = 0
  for i = 1, n do
    total = total + ((i % 31) * 3)
  end
  return total
end

local function mod31_scaled_sub_loop(n)
  local total = 0
  for i = 1, n do
    total = total - ((i % 31) * 3)
  end
  return total
end

local function mod97_sub_loop(n)
  local total = 0
  for i = 1, n do
    total = total - (i % 97)
  end
  return total
end

local function mod31_scaled_range(first, last)
  local total = 0
  for i = first, last do
    total = total + ((i % 31) * 3)
  end
  return total
end

local function expected(fn, n)
  jit.off(fn, true)
  local result = fn(n)
  jit.on(fn, true)
  return result
end

local function expect_sequence(name, fn)
  local want = expected(fn, 1000)
  jit.opt.start("hotloop=1", "hotexit=1")
  fn(20)
  fn(30)
  t.eq(fn(1000), want, name .. " reused trace result")
end

local function expect_changing_stops(name, fn)
  local stops = { 1, 2, 3, 4, 5, 20, 31, 97, 400, 800, 4000 }
  local want = {}
  jit.off(fn, true)
  for i = 1, #stops do
    want[i] = fn(stops[i])
  end
  jit.on(fn, true)
  jit.flush()
  jit.opt.start("hotloop=1", "hotexit=1")
  for pass = 1, 3 do
    for i = 1, #stops do
      t.eq(fn(stops[i]), want[i],
           name .. " changing stop " .. stops[i] .. " pass " .. pass)
    end
  end
end

local function expect_ranges(name, fn)
  local ranges = {
    { 1, 1 },
    { 2, 7 },
    { 5, 97 },
    { 13, 400 },
    { 96, 400 },
    { 97, 401 },
    { 101, 800 },
    { 333, 4000 },
  }
  local want = {}
  jit.off(fn, true)
  for i = 1, #ranges do
    want[i] = fn(ranges[i][1], ranges[i][2])
  end
  jit.on(fn, true)
  jit.flush()
  jit.opt.start("hotloop=1", "hotexit=1")
  for pass = 1, 3 do
    for i = 1, #ranges do
      local first, last = ranges[i][1], ranges[i][2]
      t.eq(fn(first, last), want[i],
           name .. " range " .. first .. ":" .. last .. " pass " .. pass)
    end
  end
end

t.truthy(select(1, jit.status()), "jit enabled")

jit.flush()
expect_sequence("mod31 scaled", mod31_scaled_loop)
expect_sequence("mod31 scaled sub", mod31_scaled_sub_loop)
expect_sequence("mod97 sub", mod97_sub_loop)

expect_changing_stops("mod31 scaled", mod31_scaled_loop)
expect_changing_stops("mod31 scaled sub", mod31_scaled_sub_loop)
expect_changing_stops("mod97 sub", mod97_sub_loop)
expect_ranges("mod31 scaled", mod31_scaled_range)
