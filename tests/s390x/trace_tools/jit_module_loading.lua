local jit = require("jit")
local t = require("tests.s390x.helpers.testlib")

t.enable_repo_jit_modules()
t.truthy(select(1, jit.status()), "jit enabled")

local verbose_path = os.tmpname()
local dump_path = os.tmpname()
local sum = 0

local function read_all(path)
  local fh = assert(io.open(path, "rb"))
  local data = fh:read("*a")
  fh:close()
  return data
end

t.with_finally(function()
  pcall(function() require("jit.v").off() end)
  pcall(function() require("jit.dump").off() end)
  jit.flush()
end, function()
  local v = require("jit.v")
  local dump = require("jit.dump")

  v.on(verbose_path)
  jit.opt.start("hotloop=2", "hotexit=2")
  for i = 1, 120 do
    sum = sum + ((i * 5) % 19)
  end
  v.off()

  jit.flush()

  dump.on("im", dump_path)
  for i = 1, 120 do
    sum = sum - ((i * 5) % 19)
  end
  dump.off()
end)

t.eq(sum, 0, "jit module loading total")

local verbose = read_all(verbose_path)
local dump = read_all(dump_path)

t.truthy(verbose:find("%[TRACE", 1, false), "jit.v emitted trace output")
t.truthy(dump:find("TRACE", 1, false), "jit.dump emitted trace output")

os.remove(verbose_path)
os.remove(dump_path)

print("jit_module_loading", #verbose, #dump)
