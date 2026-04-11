local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 4000,
  medium = 20000,
  hot = 80000,
}

local function positive_mod_const(n)
  local total = 0
  for i = 1, n do
    total = total + (i % 97)
  end
  return total
end

local function signed_mod_const(n)
  local total = 0
  for i = -n, n do
    total = total + (i % 97)
  end
  return total
end

local function branch_mod_const(n)
  local total = 0
  for i = 1, n do
    local m = i % 97
    if i % 7 == 0 then
      total = total - m
    else
      total = total + m
    end
  end
  return total
end

local function mixed_mod_const(n)
  local total = 0
  for i = 1, n do
    local a = i % 97
    local b = i % 13
    local c = i % 5
    total = total + a - b + c
  end
  return total
end

local cases = {}
local workloads = {
  { "positive_mod_const", positive_mod_const },
  { "signed_mod_const", signed_mod_const },
  { "branch_mod_const", branch_mod_const },
  { "mixed_mod_const", mixed_mod_const },
}

for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  for _, workload in ipairs(workloads) do
    local name = workload[1]
    local run = workload[2]
    local expected = run(n)
    cases[#cases + 1] = {
      workload = name,
      scale = scale,
      iterations = n,
      run = run,
      validate = function(result)
        bench.eq(result, expected, name .. "/" .. scale)
      end,
    }
  end
end

bench.run_suite({ family = "int_mod", cases = cases })
