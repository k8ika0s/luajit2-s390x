local jit = require("jit")
local profile = require("jit.profile")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")

local function work_range(first, last, total)
  total = total or 0.0
  for i = first, last do
    local v = i % 97
    if i % 7 == 0 then
      total = total - v
    else
      total = total + v
    end
  end
  return total
end

local rounds = 6
local per_round = 12000000
local expected_total = work_range(1, per_round, 0.0)

for _, mode in ipairs({ "i1", "fi1" }) do
  local total_samples = 0
  local total_callbacks = 0
  for round = 1, rounds do
    local round_samples = 0
    local round_callbacks = 0
    local total
    profile.start(mode, function(_, count)
      round_callbacks = round_callbacks + 1
      round_samples = round_samples + count
    end)
    total = work_range(1, per_round, 0.0)
    profile.stop()

    t.approx(total, expected_total, 1e-9, mode .. " total " .. round)
    t.truthy(round_callbacks > 0, mode .. " callbacks " .. round)
    t.truthy(round_samples >= round_callbacks, mode .. " samples " .. round)

    total_samples = total_samples + round_samples
    total_callbacks = total_callbacks + round_callbacks
  end

  t.truthy(total_callbacks >= rounds, mode .. " callbacks across rounds")
  t.truthy(total_samples >= total_callbacks, mode .. " samples across rounds")
end
