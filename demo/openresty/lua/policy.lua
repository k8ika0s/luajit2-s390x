local ffi = require("ffi")
local jit = require("jit")

local metrics = require("jit_metrics")

ffi.cdef([[
  int abs(int x);
]])

local _M = {}
local c_abs = ffi.C.abs

local country_penalty = {
  US = 0,
  CA = 50,
  DE = 75,
  SG = 75,
  BR = 250,
  CN = 350,
  NG = 600,
}

local merchant_penalty = {
  grocery = 0,
  fuel = 40,
  travel = 120,
  electronics = 220,
  luxury = 320,
  crypto = 600,
}

local function read_payload()
  ngx.req.read_body()
  local raw = ngx.req.get_body_data()
  if not raw or raw == "" then
    return nil, "request body is required"
  end

  local amount = tonumber(raw:match('"amount"%s*:%s*(-?%d+%.?%d*)'))
  local country = raw:match('"country"%s*:%s*"([^"]+)"')
  local merchant = raw:match('"merchant"%s*:%s*"([^"]+)"')
  local token_age_s = tonumber(raw:match('"token_age_s"%s*:%s*(%d+)'))
  if amount == nil or country == nil or merchant == nil or token_age_s == nil then
    return nil, "invalid json payload"
  end

  return amount, country, merchant, token_age_s
end

local function compute_core_score_impl(amount)
  local score = 0
  local folded = math.floor(amount + 0.5) % 97
  for _ = 1, 200 do
    score = score + c_abs(folded - 42)
  end
  return score
end

local function compute_core_score_jit(amount)
  return compute_core_score_impl(amount)
end

local function compute_core_score_interp(amount)
  return compute_core_score_impl(amount)
end

jit.off(compute_core_score_interp, true)

local function compute_score(amount, country, merchant, token_age_s, mode)
  local score
  if mode == "interp" then
    score = compute_core_score_interp(amount)
  else
    score = compute_core_score_jit(amount)
  end

  score = score + (country_penalty[country] or 150)
  score = score + (merchant_penalty[merchant] or 120)
  if token_age_s < 300 then
    score = score + 400
  elseif token_age_s < 1800 then
    score = score + 150
  end
  return score
end

local function route_result(score, mode)
  local decision = score >= 2000 and "review" or "approve"
  local jit_header = mode == "interp" and "policy-off" or "on"

  ngx.req.set_header("X-Decision", decision)
  ngx.req.set_header("X-Risk-Score", tostring(score))
  ngx.req.set_header("X-Policy-Mode", mode)
  ngx.req.set_header("X-LuaJIT", jit_header)

  if decision == "approve" then
    return ngx.exec("/__approve_dispatch")
  end
  return ngx.exec("/__review_dispatch")
end

function _M.handle_gateway_request(mode)
  local amount, country, merchant, token_age_s_or_err = read_payload()
  if not amount then
    ngx.status = ngx.HTTP_BAD_REQUEST
    ngx.header.content_type = "application/json"
    ngx.say(string.format('{"error":"%s"}', country))
    return ngx.exit(ngx.HTTP_BAD_REQUEST)
  end

  metrics.bump_policy_call(mode)

  local score = compute_score(amount, country, merchant, token_age_s_or_err, mode)
  return route_result(score, mode)
end

function _M.demo_summary()
  return table.concat({
    "This demo shows a LuaJIT-dependent gateway path running natively on IBM Z.",
    "The gateway evaluates a hot FFI-backed risk score inside OpenResty workers,",
    "then routes requests to mock approve or review backends on s390x.",
    "That programmable gateway surface was not available before the LuaJIT s390x bring-up.",
  }, " ")
end

function _M.example_payloads()
  return {
    low_risk = {
      amount = 42,
      country = "US",
      merchant = "grocery",
      token_age_s = 7200,
    },
    high_risk = {
      amount = 960,
      country = "NG",
      merchant = "crypto",
      token_age_s = 45,
    },
  }
end

return _M
