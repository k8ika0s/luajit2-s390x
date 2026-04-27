local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

ffi.cdef[[
typedef struct {
  uint32_t u32;
  uint16_t u16;
  uint8_t u8;
} u32_forward_box_t;
]]

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2", "hotexit=2")

local function expected_forwarded_sum(n)
  local total = 0
  for i = 1, n do
    total = total + ((i % 4096) * 17) + (i % 65535) + (i % 251)
  end
  return total
end

local function forwarded_sum(n)
  local box = ffi.new("u32_forward_box_t[1]")
  local total = 0
  for i = 1, n do
    box[0].u32 = (i % 4096) * 17
    box[0].u16 = i % 65535
    box[0].u8 = i % 251
    total = total + box[0].u32 + box[0].u16 + box[0].u8
  end
  return total
end

local function wrapped_negative_sum(n)
  local box = ffi.new("u32_forward_box_t[1]")
  local total = 0
  for i = 1, n do
    box[0].u32 = -i
    local value = box[0].u32
    local expected = 4294967296 - i
    if value ~= expected then
      error(string.format("wrapped u32 mismatch at %d: expected %.17g, got %.17g",
			  i, expected, value))
    end
    total = total + (value % 17)
  end
  return total
end

local function high_bit_sum(n)
  local box = ffi.new("u32_forward_box_t[1]")
  local total = 0
  for i = 1, n do
    box[0].u32 = 2147483648 + (i % 1024)
    local value = box[0].u32
    if value < 2147483648 then
      error(string.format("high-bit u32 became signed at %d: %.17g", i, value))
    end
    total = total + (value % 13)
  end
  return total
end

local function expected_wrapped_negative_sum(n)
  local total = 0
  for i = 1, n do
    total = total + ((4294967296 - i) % 17)
  end
  return total
end

local function expected_high_bit_sum(n)
  local total = 0
  for i = 1, n do
    total = total + ((2147483648 + (i % 1024)) % 13)
  end
  return total
end

for _, n in ipairs({4, 200, 4096, 12000}) do
  jit.flush()
  t.eq(forwarded_sum(n), expected_forwarded_sum(n),
       "range-proven uint32_t forwarded sum")

  jit.flush()
  t.eq(wrapped_negative_sum(n), expected_wrapped_negative_sum(n),
       "wrapped negative uint32_t sum")

  jit.flush()
  t.eq(high_bit_sum(n), expected_high_bit_sum(n),
       "high-bit uint32_t sum")
end
