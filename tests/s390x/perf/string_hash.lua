local bench = dofile("tests/s390x/perf/benchlib.lua")

local scales = {
  small = 500,
  medium = 2500,
  hot = 10000,
}

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

local function bench_collision(n)
  local total = 0
  local map = {}
  for i = 1, n do
    local s = make_collision(tags[((i - 1) % #tags) + 1])
    local v = (map[s] or 0) + 1
    map[s] = v
    total = total + v + #s
  end
  return total
end

local function bench_varied(n)
  local total = 0
  local map = {}
  for i = 1, n do
    local s = make_varied(tags[((i - 1) % #tags) + 1])
    local v = (map[s] or 0) + 3
    map[s] = v
    total = total + v + #s
  end
  return total
end

do
  local ok, jit = pcall(require, "jit")
  if ok and jit and jit.off then
    jit.off(bench_collision, true)
    jit.off(bench_varied, true)
  end
end

local cases = {}
for _, scale in ipairs(bench.scale_order(scales)) do
  local n = scales[scale]
  local collision_expected = bench_collision(n)
  local varied_expected = bench_varied(n)
  cases[#cases + 1] = {
    workload = "intern_collision_long",
    scale = scale,
    iterations = n,
    run = bench_collision,
    validate = function(result)
      bench.eq(result, collision_expected, "intern_collision_long/" .. scale)
    end,
  }
  cases[#cases + 1] = {
    workload = "intern_varied_long",
    scale = scale,
    iterations = n,
    run = bench_varied,
    validate = function(result)
      bench.eq(result, varied_expected, "intern_varied_long/" .. scale)
    end,
  }
end

bench.run_suite({ family = "string_hash", cases = cases })
