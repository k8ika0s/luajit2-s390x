local M = {}

function M.eq(actual, expected, label)
  if actual ~= expected then
    error(string.format("%s: expected %s, got %s", label or "eq", tostring(expected), tostring(actual)), 2)
  end
end

function M.approx(actual, expected, epsilon, label)
  epsilon = epsilon or 1e-9
  if math.abs(actual - expected) > epsilon then
    error(string.format("%s: expected %.17g, got %.17g", label or "approx", expected, actual), 2)
  end
end

function M.truthy(value, label)
  if not value then
    error(string.format("%s: expected truthy value", label or "truthy"), 2)
  end
end

function M.same_array(actual, expected, label)
  M.eq(#actual, #expected, (label or "same_array") .. " length")
  for i = 1, #expected do
    if actual[i] ~= expected[i] then
      error(
        string.format(
          "%s: mismatch at index %d, expected %s, got %s",
          label or "same_array",
          i,
          tostring(expected[i]),
          tostring(actual[i])
        ),
        2
      )
    end
  end
end

return M
