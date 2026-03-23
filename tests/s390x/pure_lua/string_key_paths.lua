local jit_ok, jit = pcall(require, "jit")
if jit_ok then
  jit.off()
end

local function exercise_table_strings()
  local t = { alpha = 10, nested = { ok = true } }
  assert(t.alpha == 10)
  assert(t["alpha"] == 10)
  t.beta = 20
  assert(t.beta == 20)
  assert(t["beta"] == 20)
  assert(t.nested.ok == true)
end

local function exercise_globals(i)
  _G.__s390x_vm_probe = "value-" .. i
  assert(_G.__s390x_vm_probe == "value-" .. i)
end

for i = 1, 2000 do
  exercise_table_strings()
  exercise_globals(i)
end

collectgarbage()
exercise_table_strings()
exercise_globals("final")

print("string_key_paths ok")
