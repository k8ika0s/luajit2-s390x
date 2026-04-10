local t = dofile("tests/s390x/helpers/testlib.lua")

local decimal = require("s390x.experimental.decimal")

local function hex_of(bytes)
  local parts = {}
  local i
  for i = 1, #bytes do
    parts[i] = string.format("%02x", string.byte(bytes, i))
  end
  return table.concat(parts)
end

local caps = decimal.capabilities()
t.truthy(type(caps) == "table", "capabilities table")
t.truthy(caps.available, "module available")
t.truthy(caps.software, "software backend enabled")
t.truthy(caps.packed_decimal, "packed decimal supported")
t.truthy(caps.zoned_decimal, "zoned decimal supported")

local a = decimal.new("00123.4500")
t.eq(decimal.tostring(a), "123.45", "canonical decimal tostring")
t.eq(tostring(a), "123.45", "decimal __tostring")

local b = decimal.new("-0.0100")
t.eq(decimal.tostring(b), "-0.01", "negative fractional canonicalization")

t.eq(decimal.cmp(decimal.new("1.2"), decimal.new("1.20")), 0, "cmp equal")
t.eq(decimal.cmp(decimal.new("-5"), decimal.new("4.99")), -1, "cmp signed")
t.eq(decimal.cmp(decimal.new("10.5"), decimal.new("10.49")), 1, "cmp greater")

local sum = decimal.add(decimal.new("12.34"), decimal.new("0.66"))
t.eq(decimal.tostring(sum), "13", "add carry into integer")

local sum_signed = decimal.add(decimal.new("-2.50"), decimal.new("1.25"))
t.eq(decimal.tostring(sum_signed), "-1.25", "add signed operands")

local diff = decimal.sub(decimal.new("100.00"), decimal.new("1.25"))
t.eq(decimal.tostring(diff), "98.75", "sub fractional borrow")

local diff_signed = decimal.sub(decimal.new("1.25"), decimal.new("2.50"))
t.eq(decimal.tostring(diff_signed), "-1.25", "sub negative result")

local packed = decimal.to_packed(decimal.new("-123.45"), 5)
t.eq(hex_of(packed), "12345d", "packed encoding")
t.eq(decimal.tostring(decimal.from_packed(packed, 2)), "-123.45",
     "packed roundtrip")
t.eq(decimal.packed_to_string(packed, 2), "-123.45",
     "direct packed decode to string")
t.eq(hex_of(decimal.string_to_packed("-123.45", 5)), "12345d",
     "direct string to packed")
t.eq(hex_of(decimal.string_to_packed("00123.4500", 5)), "12345c",
     "direct string to packed normalizes padded fractional input")
t.eq(hex_of(decimal.string_to_packed("-0.000", 1)), "0c",
     "direct string to packed normalizes negative zero")
t.eq(hex_of(decimal.packed_rescale(packed, 2)), "12345d",
     "packed rescale keeps canonical bytes")
t.eq(hex_of(decimal.packed_rescale(packed, 2, 7)), "0012345d",
     "packed rescale widens digits")
local packed_positive_f = string.char(0x12, 0x3f)
t.eq(hex_of(decimal.packed_rescale(packed_positive_f, 0, 3)), "123c",
     "packed rescale canonicalizes positive sign")
local packed_negative_zero = string.char(0x00, 0x0d)
t.eq(hex_of(decimal.packed_rescale(packed_negative_zero, 0, 3)), "000c",
     "packed rescale canonicalizes negative zero")

local packed_small = decimal.to_packed(decimal.new("12"), 2)
t.eq(hex_of(packed_small), "012c", "packed left-pad digits")

local packed_trailing = string.char(0x12, 0x0c)
t.eq(decimal.tostring(decimal.from_packed(packed_trailing, 2)), "1.2",
     "packed decode normalizes trailing fraction zeros")
t.eq(decimal.packed_to_string(packed_trailing, 2), "1.2",
     "direct packed decode normalizes trailing zeros")
t.eq(hex_of(decimal.packed_rescale(packed_trailing, 2)), "012c",
     "packed rescale canonicalizes trailing fraction zeros")

local zoned = decimal.to_zoned(decimal.new("987.6"), 4)
t.eq(hex_of(zoned), "f9f8f7c6", "zoned encoding")
t.eq(decimal.tostring(decimal.from_zoned(zoned, 1)), "987.6",
     "zoned roundtrip")
t.eq(decimal.zoned_to_string(zoned, 1), "987.6",
     "direct zoned decode to string")
t.eq(hex_of(decimal.string_to_zoned("987.6", 4)), "f9f8f7c6",
     "direct string to zoned")
t.eq(hex_of(decimal.string_to_zoned("00987.600", 4)), "f9f8f7c6",
     "direct string to zoned normalizes padded fractional input")
t.eq(hex_of(decimal.string_to_zoned("-0.000", 1)), "c0",
     "direct string to zoned normalizes negative zero")

local zero = decimal.from_packed(string.char(0x0c), 0)
t.eq(decimal.tostring(zero), "0", "packed zero")

local zoned_trailing = string.char(0xf4, 0xf5, 0xf6, 0xf0, 0xc0)
t.eq(decimal.tostring(decimal.from_zoned(zoned_trailing, 3)), "45.6",
     "zoned decode normalizes trailing fraction zeros")
t.eq(decimal.zoned_to_string(zoned_trailing, 3), "45.6",
     "direct zoned decode normalizes trailing zeros")

local zero_sum = decimal.sub(decimal.new("1.20"), decimal.new("1.2"))
t.eq(decimal.tostring(zero_sum), "0", "sub exact zero")
