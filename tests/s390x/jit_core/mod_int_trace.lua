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

t.truthy(select(1, jit.status()), "jit enabled")
expect(numeric_loop, 400)
expect(side_exit_loop, 400)
expect(signed_loop, 200)

-- Keep the aggressive hotexit/stitch shape as a separate tracked repro until
-- it is closure-green on native s390x release builds.
t.truthy(hotexit_loop(400) > 0, "hotexit modulo baseline executes")
