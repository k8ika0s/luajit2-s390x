local ffi = require("ffi")
local jit = require("jit")

jit.opt.start("hotloop=2", "hotexit=2")

ffi.cdef[[
typedef struct { unsigned short a; unsigned int b; unsigned char c; } packed_u_t;
]]

local function expected(n)
  local total = 0
  for i = 1, n do
    total = total + (i % 65535) + ((i % 4096) * 17) + (i % 251)
  end
  return total
end

local function mixed_width_sum(n)
  local slot = ffi.new("packed_u_t[1]")
  local total = 0
  for i = 1, n do
    slot[0].a = i % 65535
    slot[0].b = (i % 4096) * 17
    slot[0].c = i % 251
    total = total + slot[0].a + slot[0].b + slot[0].c
  end
  return total
end

local function observed_after_loop(n)
  local slot = ffi.new("packed_u_t[1]")
  local total = 0
  for i = 1, n do
    slot[0].a = i % 65535
    slot[0].b = (i % 4096) * 17
    slot[0].c = i % 251
    total = total + slot[0].a + slot[0].b + slot[0].c
  end
  return total, slot[0].a, slot[0].b, slot[0].c
end

for _, n in ipairs({1, 251, 4096, 32000, 70000}) do
  jit.flush()
  local got = mixed_width_sum(n)
  local exp = expected(n)
  assert(got == exp,
	 string.format("mixed_width_sum(%d): expected %.17g, got %.17g",
		       n, exp, got))
end

jit.flush()
local total, a, b, c = observed_after_loop(32000)
assert(total == expected(32000), "observed_after_loop total mismatch")
assert(a == 32000 % 65535, "observed_after_loop a mismatch")
assert(b == (32000 % 4096) * 17, "observed_after_loop b mismatch")
assert(c == 32000 % 251, "observed_after_loop c mismatch")

print("ffi_cdata_mixed_width_loop_sum PASS")
