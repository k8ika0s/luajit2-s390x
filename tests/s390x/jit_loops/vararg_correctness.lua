local jit = require("jit")
local jutil = require("jit.util")
local t = require("tests.s390x.helpers.testlib")

local function retconst(...)
  return 42
end

local function retconst_loop(n)
  local total = 0
  for i = 1, n do
    total = total + retconst(i, i + 1, i + 2, i + 3)
  end
  return total
end

local function dynamic_select_four(...)
  local total = 0
  local n = select("#", ...)
  for i = 1, n do
    total = total + select(i, ...)
  end
  return total
end

local function dynamic_select_loop(n)
  local total = 0
  for i = 1, n do
    total = total + dynamic_select_four(i, i + 1, i + 2, i + 3)
  end
  return total
end

local function vararg_paths_sum(...)
  local total = 0
  for i = 1, select("#", ...) do
    total = total + select(i, ...)
  end
  return total
end

local function vararg_paths_sum_loop(n)
  local bit = require("bit")
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + vararg_paths_sum(1, 2, 3, ((i - 1) % 17) + 1))
  end
  return result
end

local function expected_vararg_paths_sum_loop(n)
  local bit = require("bit")
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + 7 + ((i - 1) % 17))
  end
  return result
end

local function count_plus_index(...)
  local n = select("#", ...)
  return select(n, ...) + n
end

local function count_plus_index_loop(n)
  local total = 0
  for i = 1, n do
    total = total + count_plus_index(i, i + 1, i + 2, i + 3)
  end
  return total
end

local function retlast_select(...)
  return select(select("#", ...), ...)
end

local function retlast_select_loop(n)
  local bit = require("bit")
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + retlast_select(1, 2, 3, ((i - 1) % 17) + 1))
  end
  return result
end

local function expected_retlast_select_loop(n)
  local bit = require("bit")
  local result = 0
  for i = 1, n do
    result = bit.tobit(result + ((i - 1) % 17) + 1)
  end
  return result
end

local function retlast_select_high_modulo_loop()
  local bit = require("bit")
  local result = 0
  for i = 2147483500, 2147483646 do
    result = bit.tobit(result + retlast_select(1, 2, 3, (i % 17) + 1))
  end
  return result
end

local builtin_select = select

local function retlast_select_global_mutation_loop(n)
  return t.with_finally(function()
    _G.select = builtin_select
  end, function()
    local total = 0
    _G.select = builtin_select
    for i = 1, n do
      if i == 85 then
        _G.select = function(idx, ...)
          if idx == "#" then
            return builtin_select("#", ...)
          end
          return 1000 + builtin_select(idx, ...)
        end
      end
      total = total + retlast_select(1, 2, 3, i)
    end
    return total
  end)
end

local function expected_retlast_select_global_mutation(n)
  local total = 0
  for i = 1, n do
    total = total + (i < 85 and i or 1000 + i)
  end
  return total
end

local function builtin_dynamic_select_loop(n)
  local bit = require("bit")
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + select(((i - 1) % 4) + 1, 1, 2, 3, 4))
  end
  return total
end

local function expected_builtin_dynamic_select(n)
  local bit = require("bit")
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + ((i - 1) % 4) + 1)
  end
  return total
end

local function builtin_dynamic_select_progression_loop(n)
  local bit = require("bit")
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + select(((i - 1) % 3) + 1, 3, 5, 7))
  end
  return total
end

local function expected_builtin_dynamic_select_progression(n)
  local bit = require("bit")
  local total = 0
  for i = 1, n do
    total = bit.tobit(total + (((i - 1) % 3) * 2) + 3)
  end
  return total
end

local function second_or_count(...)
  local n = select("#", ...)
  if n >= 2 then
    return select(2, ...) + n
  end
  return n
end

local function changed_count_loop(n)
  local total = 0
  for i = 1, n do
    if i == 85 then
      total = total + second_or_count(i)
    elseif i % 41 == 0 then
      total = total + second_or_count(i, i + 1)
    else
      total = total + second_or_count(i, i + 1, i + 2, i + 3)
    end
  end
  return total
end

local function expected_changed_count(n)
  local total = 0
  for i = 1, n do
    if i == 85 then
      total = total + 1
    elseif i % 41 == 0 then
      total = total + i + 3
    else
      total = total + i + 5
    end
  end
  return total
end

local function ret_first_two(...)
  return select(1, ...), select(2, ...)
end

local function fixed_arity_return_loop(n)
  local total = 0
  for i = 1, n do
    local a, b = ret_first_two(i, i + 1, i + 2, i + 3)
    total = total + a * 3 + b
  end
  return total
end

local table_upvalue_ops = {
  pick = function(x)
    return x
  end,
}

local function table_upvalue_vararg(...)
  return table_upvalue_ops.pick(select(1, ...))
end

local function table_upvalue_mutation_loop(n)
  table_upvalue_ops.pick = function(x)
    return x
  end
  local total = 0
  for i = 1, n do
    if i == 85 then
      table_upvalue_ops.pick = function(x)
        return x * 2
      end
    end
    total = total + table_upvalue_vararg(i, i + 1, i + 2, i + 3)
  end
  return total
end

local function expected_table_upvalue_mutation(n)
  local total = 0
  for i = 1, n do
    total = total + (i < 85 and i or i * 2)
  end
  return total
end

local function sum_4i_plus_c(n, c)
  return 2 * n * (n + 1) + c * n
end

local function sum_i_plus_c(n, c)
  return n * (n + 1) / 2 + c * n
end

local function run_traced(label, fn, expected)
  jit.flush()
  local cap = t.trace_counter_capture_lite()
  local actual = t.with_finally(function()
    cap.stop()
  end, fn)
  t.eq(actual, expected, label)
  t.truthy(cap.stop_count > 0, label .. " trace stop")
  t.truthy(jutil.traceinfo(1) ~= nil, label .. " traceinfo")
end

local function run_checked(label, fn, expected)
  jit.flush()
  local actual = fn()
  t.eq(actual, expected, label)
end

local function run_joff(fn)
  local enabled = select(1, jit.status())
  local ok, result
  if enabled then
    jit.off()
  end
  ok, result = pcall(fn)
  if enabled then
    jit.on()
    jit.flush()
  end
  if not ok then
    error(result, 0)
  end
  return result
end

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

run_checked("constant-return vararg callee", function()
  return retconst_loop(160)
end, 160 * 42)

run_traced("dynamic select stable four varargs", function()
  return dynamic_select_loop(160)
end, sum_4i_plus_c(160, 6))

run_traced("vararg_paths dynamic sum shape", function()
  return vararg_paths_sum_loop(160)
end, expected_vararg_paths_sum_loop(160))

run_traced("select count plus indexed access", function()
  return count_plus_index_loop(160)
end, sum_i_plus_c(160, 7))

run_traced("canonical retlast select", function()
  return retlast_select_loop(160)
end, expected_retlast_select_loop(160))

run_traced("canonical retlast select high positive modulo", function()
  return retlast_select_high_modulo_loop()
end, run_joff(retlast_select_high_modulo_loop))

run_traced("canonical retlast select global mutation exits", function()
  return retlast_select_global_mutation_loop(160)
end, expected_retlast_select_global_mutation(160))

run_traced("builtin dynamic select fixed args", function()
  return builtin_dynamic_select_loop(160)
end, expected_builtin_dynamic_select(160))

run_traced("builtin dynamic select integer progression", function()
  return builtin_dynamic_select_progression_loop(160)
end, expected_builtin_dynamic_select_progression(160))

run_traced("changed vararg count exits correctly", function()
  return changed_count_loop(160)
end, expected_changed_count(160))

run_traced("vararg callee fixed-arity return", function()
  return fixed_arity_return_loop(160)
end, sum_4i_plus_c(160, 1))

run_traced("immutable table upvalue field mutation exits", function()
  return table_upvalue_mutation_loop(160)
end, expected_table_upvalue_mutation(160))
