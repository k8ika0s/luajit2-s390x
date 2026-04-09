local t = require("tests.s390x.helpers.testlib")

local lower_tokens = {}
for tok in string.gmatch(
  "2026-04-08T13:08:08Z level=INFO req=400184 path=/svc/v2/item/08 user=alpha08 zone=us-east-1 latency_ms=70 token=beta08 gamma08",
  "%a+"
) do
  lower_tokens[#lower_tokens + 1] = string.lower(tok)
end

local upper_tokens = {}
for tok in string.gmatch(
  "400184 -- 210 .. alpha08 !! 70 ?? beta08 :: 400254 ## gamma08 %% 400283 $$ delta08",
  "%a+"
) do
  upper_tokens[#upper_tokens + 1] = string.upper(tok)
end

local head = string.lower(string.match("::08:: x-trace-08 alpha08-token/400184", "%a+"))
local lower = string.lower("LuaJIT-S390x-Combo-08-AbCdEfGhIjKlMnOpQrStUvWxYz")
local upper = string.upper("LuaJIT-S390x-Combo-08-AbCdEfGhIjKlMnOpQrStUvWxYz")
local a, b = string.find("xxneedle08needlezz", "needle08", 1, true)
local rev = string.reverse("s390x-control-08-reverse-pass")

t.eq(table.concat(lower_tokens, ","), "t,z,level,info,req,path,svc,v,item,user,alpha,zone,us,east,latency,ms,token,beta,gamma", "combo lower tokens")
t.eq(table.concat(upper_tokens, ","), "ALPHA,BETA,GAMMA,DELTA", "combo upper tokens")
t.eq(head, "x", "combo header lower")
t.eq(lower, "luajit-s390x-combo-08-abcdefghijklmnopqrstuvwxyz", "combo lower")
t.eq(upper, "LUAJIT-S390X-COMBO-08-ABCDEFGHIJKLMNOPQRSTUVWXYZ", "combo upper")
t.eq(a, 3, "combo find start")
t.eq(b, 10, "combo find stop")
t.eq(rev, "ssap-esrever-80-lortnoc-x093s", "combo reverse")

local function run(n)
  local total = 0
  for i = 1, n do
    local tag = string.format("%02d", ((i - 1) % 32) + 1)
    local tok = string.lower(string.match("::" .. tag .. ":: x-trace-" .. tag .. " alpha" .. tag .. "-token/" .. (400000 + i), "%a+"))
    local upsum = 0
    for word in string.gmatch("400184 -- 210 .. alpha" .. tag .. " !! 70 ?? beta" .. tag .. " :: 400254 ## gamma" .. tag .. " %% 400283 $$ delta" .. tag, "%a+") do
      upsum = upsum + #string.upper(word)
    end
    total = total + #tok + upsum
  end
  return total
end

t.eq(run(64), 1280, "combo total")
