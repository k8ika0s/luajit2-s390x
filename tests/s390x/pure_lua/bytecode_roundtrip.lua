local t = require("tests.s390x.helpers.testlib")

local function payload(a, b, c)
  return (a + b) * c
end

local dumped = string.dump(payload)
local loaded = assert(loadstring(dumped))

t.eq(loaded(2, 3, 4), 20, "string.dump roundtrip")

local ok, result = pcall(function()
  return loaded(5, 6, 7)
end)
t.truthy(ok, "pcall over dumped function")
t.eq(result, 77, "pcall result")
