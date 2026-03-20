local jit = require("jit")
local profile = require("jit.profile")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")

local samples = 0
profile.start("fi1", function(_, count)
  samples = samples + count
end)

local total = 0.0
for i = 1, 80000000 do
  total = total + i
end

profile.stop()

t.truthy(samples > 0, "profile loop samples")
t.truthy(total > 0, "profile loop total")
