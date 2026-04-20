local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2", "minstitch=1")

local function array_holes(tab, n)
  local total = 0
  for _ = 1, n do
    for k, v in pairs(tab) do
      total = total + k * 100 + v
    end
  end
  return total
end

local function hash_key_values(tab, n)
  local total = 0
  for _ = 1, n do
    for k, v in pairs(tab) do
      total = total + #k * 100 + v
    end
  end
  return total
end

local function explicit_next_sum(tab, n)
  local total = 0
  for _ = 1, n do
    local k, v = next(tab, nil)
    while k ~= nil do
      if type(k) == "number" then
        total = total + k * 100 + v
      else
        total = total + #k * 100 + v
      end
      k, v = next(tab, k)
    end
  end
  return total
end

local function mutation_sum(tab, n)
  local total = 0
  for _ = 1, n do
    for _, v in pairs(tab) do
      total = total + v
    end
  end
  return total
end

local array_tab = { [1] = 10, [3] = 30, [5] = 50 }
local hash_tab = { alpha = 1, beta = 2, gamma = 3, delta = 4, epsilon = 5 }
local mixed_tab = { [1] = 10, [4] = 40, omega = 7, zeta = 8 }

t.eq(array_holes(array_tab, 200), (110 + 330 + 550) * 200,
     "pairs array holes preserve visible key/value")
t.eq(hash_key_values(hash_tab, 200),
     ((5 + 4 + 5 + 5 + 7) * 100 + 1 + 2 + 3 + 4 + 5) * 200,
     "pairs hash keys are materialized")
t.eq(explicit_next_sum(mixed_tab, 200),
     (110 + 440 + 5 * 100 + 7 + 4 * 100 + 8) * 200,
     "explicit next mixed table")

local mutable = { a = 1, b = 2, c = 3, d = 4, e = 5 }
t.eq(mutation_sum(mutable, 200), 15 * 200, "warm mutation base")
mutable.f = 6
t.eq(mutation_sum(mutable, 200), 21 * 200, "mutation invalidates iterator guards")
t.eq(mutation_sum({}, 200), 0, "terminal nil on empty table")

print("iterator contract ok")
