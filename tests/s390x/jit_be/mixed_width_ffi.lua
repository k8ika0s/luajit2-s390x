local ffi = require("ffi")
local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.truthy(select(1, jit.status()), "jit enabled")
jit.opt.start("hotloop=2")

ffi.cdef([[
typedef struct packed_pair {
  uint8_t a;
  uint32_t b;
  uint16_t c;
} packed_pair;
]])

local pair = ffi.new("packed_pair")
pair.a = 7
pair.b = 0x11223344
pair.c = 0x5566

local total = 0
for _ = 1, 100 do
  total = total + pair.a + tonumber(pair.b) + pair.c
end

t.eq(total, 100 * (7 + 0x11223344 + 0x5566), "mixed width ffi")
