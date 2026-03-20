local jit = require("jit")
local util = require("jit.util")

local function dump_snap(tr, sn)
  local snap = util.tracesnap(tr, sn)
  if not snap then
    return false
  end
  io.write(string.format("SNAP tr=%d sn=%d ref=%d nslots=%d map=", tr, sn, snap[0], snap[1]))
  local parts = {}
  for i = 2, #snap do
    parts[#parts + 1] = tostring(snap[i])
  end
  io.write(table.concat(parts, ","))
  io.write("\n")
  return true
end

local function dump_trace(tr)
  local info = util.traceinfo(tr)
  if not info then
    return false
  end
  print(string.format(
    "TRACEINFO tr=%d link=%s type=%s nins=%s nk=%s nexit=%s",
    tr,
    tostring(info.link),
    tostring(info.linktype),
    tostring(info.nins),
    tostring(info.nk),
    tostring(info.nexit)
  ))
  for kref = -1, -info.nk, -1 do
    local k, kt, slot = util.tracek(tr, kref)
    print(string.format(
      "KREF tr=%d ref=%d type=%s slot=%s value=%s",
      tr,
      kref,
      tostring(kt),
      tostring(slot),
      tostring(k)
    ))
  end
  for ins = 0, info.nins - 1 do
    local mode, ot, op1, op2, prev = util.traceir(tr, ins)
    local opidx = math.floor(ot / 256)
    print(string.format(
      "IR tr=%d ins=%d opidx=%d ot=%d mode=%d op1=%d op2=%d prev=%d",
      tr, ins, opidx, ot, mode, op1, op2, prev
    ))
  end
  for sn = 0, info.nexit - 1 do
    dump_snap(tr, sn)
  end
  return true
end

jit.opt.start("hotloop=2", "hotexit=1", "minstitch=1")

local keys = {}
jit.off()
for i = 1, 20 do
  keys[i] = "a" .. i
end
jit.on()

local tab = {}
for i = 1, 100 do
  local s = keys[((i - 1) % 20) + 1]
  tab[s] = i
end

jit.off(true, true)

for tr = 1, 4 do
  if not dump_trace(tr) then
    break
  end
end

print("done", tab.a1, tab.a20)
