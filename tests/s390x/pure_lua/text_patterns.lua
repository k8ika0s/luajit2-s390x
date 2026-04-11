local t = require("tests.s390x.helpers.testlib")

local alpha = string.match("AlphaBeta123", "%a+")
local digits = string.match("abc12345xyz", "%d+")
local alpha_seek = string.match("__99--AlphaBeta123", "%a+")
local digits_seek = string.match("alpha--12345xyz", "%d+")
local punct = string.match("!!??::08alpha", "%p+")
local punct_seek = string.match("alpha08 --!!??::", "%p+")
local nonpunct = string.match("Alpha42!!", "%P+")
local nonpunct_seek = string.match("!!??Alpha42", "%P+")
local words = {}
local sparse_words = {}
local sparse_punct = {}
local sparse_nonpunct = {}

for tok in string.gmatch("alpha 123 beta gamma 456 delta", "%a+") do
  words[#words + 1] = tok
end

t.eq(alpha, "AlphaBeta", "pattern alpha")
t.eq(digits, "12345", "pattern digits")
t.eq(alpha_seek, "AlphaBeta", "pattern alpha seek")
t.eq(digits_seek, "12345", "pattern digits seek")
t.eq(punct, "!!??::", "pattern punctuation")
t.eq(punct_seek, "--!!??::", "pattern punctuation seek")
t.eq(nonpunct, "Alpha42", "pattern non-punctuation")
t.eq(nonpunct_seek, "Alpha42", "pattern non-punctuation seek")
t.eq(table.concat(words, ","), "alpha,beta,gamma,delta", "pattern gmatch words")

for tok in string.gmatch("123--alpha !! 456??beta :: 789##gamma %% 012$$delta", "%a+") do
  sparse_words[#sparse_words + 1] = tok
end

t.eq(table.concat(sparse_words, ","), "alpha,beta,gamma,delta",
     "pattern sparse gmatch words")

for tok in string.gmatch("alpha!!beta--::gamma##%%delta$$//", "%p+") do
  sparse_punct[#sparse_punct + 1] = tok
end

t.eq(table.concat(sparse_punct, ","), "!!,--::,##%%,$$//",
     "pattern sparse gmatch punctuation")

for tok in string.gmatch("!!alpha??beta::gamma##delta", "%P+") do
  sparse_nonpunct[#sparse_nonpunct + 1] = tok
end

t.eq(table.concat(sparse_nonpunct, ","), "alpha,beta,gamma,delta",
     "pattern sparse gmatch non-punctuation")

local function run(n)
  local total = 0
  for i = 1, n do
    local a = string.match("TokenRun" .. i .. "X", "%a+")
    local d = string.match("id-" .. i .. "-42", "%d+")
    local p = string.match("id-" .. i .. "-!!done", "%p+")
    local q = string.match("!!TokenRun" .. i .. "X", "%P+")
    total = total + #a + #d + #p + #q
  end
  return total
end

t.eq(run(120), 2664, "pattern total")
