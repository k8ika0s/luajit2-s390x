local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

GLOBAL_S390X_COUNTER = 1

local function run()
  local state = {
    alpha = 3,
    beta = 5,
    arr = { 7, 11 },
    nested = { left = 13, right = 17 },
  }
  local total = 0
  for i = 1, 120 do
    GLOBAL_S390X_COUNTER = GLOBAL_S390X_COUNTER + 1
    state.alpha = state.alpha + 1
    state.arr[1] = state.arr[1] + 2
    total = total
      + GLOBAL_S390X_COUNTER
      + state.alpha
      + state.beta
      + state.arr[1]
      + state.arr[2]
      + state.nested.left
      + state.nested.right
      + _G["GLOBAL_S390X_COUNTER"]
  end
  return total, state.alpha, state.arr[1], GLOBAL_S390X_COUNTER
end

local expected_total, expected_alpha, expected_arr1, expected_global = run()
GLOBAL_S390X_COUNTER = 1

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")

local total, alpha, arr1, global_counter = run()
t.eq(total, expected_total, "string/global total")
t.eq(alpha, expected_alpha, "string field store")
t.eq(arr1, expected_arr1, "table bytecode store")
t.eq(global_counter, expected_global, "global string path")
