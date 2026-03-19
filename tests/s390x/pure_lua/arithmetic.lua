local t = require("tests.s390x.helpers.testlib")

local function fold(a, b)
  return {
    add = a + b,
    sub = a - b,
    mul = a * b,
    div = a / b,
    mod = a % b,
  }
end

local ints = fold(37, 5)
t.eq(ints.add, 42, "int add")
t.eq(ints.sub, 32, "int sub")
t.eq(ints.mul, 185, "int mul")
t.approx(ints.div, 7.4, 1e-12, "int div")
t.eq(ints.mod, 2, "int mod")

local nums = fold(7.25, 2.0)
t.approx(nums.add, 9.25, 1e-12, "num add")
t.approx(nums.sub, 5.25, 1e-12, "num sub")
t.approx(nums.mul, 14.5, 1e-12, "num mul")
t.approx(nums.div, 3.625, 1e-12, "num div")
t.approx(nums.mod, 1.25, 1e-12, "num mod")

local neg = (-3) % 2
t.eq(neg, 1, "negative modulo")

local mixed = tostring(2 ^ 10)
t.eq(mixed, "1024", "pow tostring")
