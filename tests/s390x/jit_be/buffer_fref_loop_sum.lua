local buffer = require("string.buffer")
local jit = require("jit")

jit.opt.start("hotloop=2", "hotexit=2")

local function expected(n)
  local total = 0
  for i = 1, n do
    total = total + 6 - (i % 3)
  end
  return total
end

local function buffer_len_sum(n)
  local buf = buffer.new()
  local total = 0
  for i = 1, n do
    buf:reset()
    buf:put("abcdef")
    buf:skip(i % 3)
    total = total + #buf
  end
  return total
end

local function observed_after_loop(n)
  local buf = buffer.new()
  local total = 0
  for i = 1, n do
    buf:reset()
    buf:put("abcdef")
    buf:skip(i % 3)
    total = total + #buf
  end
  return total, tostring(buf), #buf
end

for _, n in ipairs({1, 2, 3, 251, 4096, 32000, 1000000}) do
  jit.flush()
  local got = buffer_len_sum(n)
  local exp = expected(n)
  assert(got == exp,
	 string.format("buffer_len_sum(%d): expected %d, got %d",
		       n, exp, got))
end

jit.flush()
local observed_n = 32000
local total, text, len = observed_after_loop(observed_n)
local skip = observed_n % 3
local expected_text = string.sub("abcdef", skip + 1)
assert(total == expected(observed_n), "observed_after_loop total mismatch")
assert(text == expected_text, "observed_after_loop text mismatch: " .. text)
assert(len == #expected_text, "observed_after_loop len mismatch: " .. tostring(len))

print("buffer_fref_loop_sum PASS")
