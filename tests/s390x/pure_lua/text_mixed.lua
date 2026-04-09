local t = require("tests.s390x.helpers.testlib")

local log_words = {}
for tok in string.gmatch(
  "2026-04-08T12:30:01Z level=INFO req=300017 path=/svc/v1/order/08 status=210 user=alpha08 zone=us-west-3 latency_ms=27 token=beta08 gamma08",
  "%a+"
) do
  log_words[#log_words + 1] = tok
end

local sparse_words = {}
for tok in string.gmatch(
  "300017 -- 210 .. alpha08 !! 27 ?? beta08 :: 300044 ## gamma08 %% 300227 $$ delta08",
  "%a+"
) do
  sparse_words[#sparse_words + 1] = tok
end

local header = string.match("::08:: x-trace-08 alpha08-token/300017", "%a+")
local a, b = string.find("zzneedle08needlezz", "needle08", 1, true)
local lower = string.lower("LuaJIT-S390x-Mixed-08-AbCdEf")
local upper = string.upper("LuaJIT-S390x-Mixed-08-AbCdEf")

t.eq(table.concat(log_words, ","), "T,Z,level,INFO,req,path,svc,v,order,status,user,alpha,zone,us,west,latency,ms,token,beta,gamma", "mixed log words")
t.eq(table.concat(sparse_words, ","), "alpha,beta,gamma,delta", "mixed sparse words")
t.eq(header, "x", "mixed header match")
t.eq(a, 3, "mixed find start")
t.eq(b, 10, "mixed find stop")
t.eq(lower, "luajit-s390x-mixed-08-abcdef", "mixed lower")
t.eq(upper, "LUAJIT-S390X-MIXED-08-ABCDEF", "mixed upper")

local function run(n)
  local total = 0
  for i = 1, n do
    local tag = string.format("%02d", ((i - 1) % 32) + 1)
    local header_match = string.match("::" .. tag .. ":: x-trace-" .. tag .. " alpha" .. tag .. "-token/" .. (300000 + i), "%a+")
    for tok in string.gmatch("300017 -- 210 .. alpha" .. tag .. " !! 27 ?? beta" .. tag .. " :: 300044 ## gamma" .. tag .. " %% 300227 $$ delta" .. tag, "%a+") do
      total = total + #tok
    end
    total = total + #header_match
  end
  return total
end

t.eq(run(64), 1280, "mixed total")
