local t = require("tests.s390x.helpers.testlib")

local tags = {}
for i = 1, 64 do
  tags[i] = string.format("%04x", i - 1)
end

local function make_collision(tag)
  return "HEAD" ..
         string.rep("a", 43) ..
         "QFIX" ..
         string.rep("b", 43) ..
         "MFIX" ..
         string.rep("c", 46) ..
         tag ..
         string.rep("d", 40) ..
         "TAIL"
end

local function make_varied(tag)
  return "H" .. tag ..
         string.rep("m", 43) ..
         "Q" .. tag ..
         string.rep("n", 43) ..
         "M" .. tag ..
         string.rep("p", 46) ..
         tag ..
         string.rep("q", 40) ..
         "T" .. tag
end

local map = {}
local total = 0
for i = 1, #tags do
  local collision = make_collision(tags[i])
  local varied = make_varied(tags[i])
  map[collision] = i
  map[varied] = i * 3
  total = total + #collision + #varied
end

for i = 1, #tags do
  t.eq(map[make_collision(tags[i])], i, "collision lookup " .. i)
  t.eq(map[make_varied(tags[i])], i * 3, "varied lookup " .. i)
end

t.eq(total, 24832, "string hash total bytes")
