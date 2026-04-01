local bench = dofile("tests/s390x/perf/benchlib.lua")

local scale_order = { "small", "medium", "hot" }
local scales = {
  small = 1,
  medium = 5,
  hot = 20,
}

local function add_phi_only(chunks)
  local total = 0
  for _ = 1, chunks do
    for i = 1, 200 do
      total = total + i + 3
    end
  end
  return total
end

local cases = {}
for _, scale in ipairs(scale_order) do
  local chunks = scales[scale]
  local expected = add_phi_only(chunks)
  cases[#cases + 1] = {
    workload = "add_phi_only",
    scale = scale,
    iterations = chunks,
    run = add_phi_only,
    validate = function(result)
      bench.eq(result, expected, "add_phi_only/" .. scale)
    end,
  }
end

bench.run_suite({ family = "int_add_phi_only", cases = cases })
