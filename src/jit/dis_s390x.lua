----------------------------------------------------------------------------
-- LuaJIT s390x disassembler module.
--
-- Copyright (C) 2005-2026 Mike Pall. All rights reserved.
-- Released under the MIT license. See Copyright Notice in luajit.h
----------------------------------------------------------------------------
-- This is a helper module used by the LuaJIT machine code dumper module.
--
-- The full s390x instruction decoder is not implemented yet. Keep this
-- module valid and useful for bring-up by providing:
-- 1. stable register names for IR dump annotations
-- 2. a simple machine-code word dump for trace debugging
----------------------------------------------------------------------------

local byte, format = string.byte, string.format

local map_gpr = {
  "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
}

local map_fpr = {
  "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
  "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
}

local function regname(r)
  if r < 16 then return map_gpr[r+1] end
  if r < 32 then return map_fpr[r-15] end
  return "?"
end

local function u32be(code, pos)
  local b1 = byte(code, pos+1) or 0
  local b2 = byte(code, pos+2) or 0
  local b3 = byte(code, pos+3) or 0
  local b4 = byte(code, pos+4) or 0
  return ((b1 * 256 + b2) * 256 + b3) * 256 + b4
end

local function find_symbol(ctx, addr)
  local sym = ctx.symtab and ctx.symtab[addr]
  if sym then
    ctx.out(format("->%s:\n", sym))
  end
end

local function disass_block(ctx, ofs, len)
  if not ofs then ofs = 0 end
  local stop = len and (ofs + len) or #ctx.code
  local pos = ofs
  while pos < stop do
    local addr = ctx.addr + pos
    local rem = stop - pos
    find_symbol(ctx, addr)
    if rem >= 4 then
      local w = u32be(ctx.code, pos)
      ctx.out(format("%08x  %08x  .word 0x%08x\n", addr, w, w))
      pos = pos + 4
    else
      local bytes = {}
      local i
      for i = 1, rem do
        bytes[i] = format("%02x", byte(ctx.code, pos+i) or 0)
      end
      ctx.out(format("%08x  %-11s  .byte %s\n",
        addr, table.concat(bytes, " "), table.concat(bytes, ", ")))
      pos = stop
    end
  end
end

local function create(code, addr, out)
  local ctx = {}
  ctx.code = code
  ctx.addr = addr or 0
  ctx.out = out or io.write
  ctx.symtab = {}
  ctx.disass = disass_block
  ctx.hexdump = 8
  return ctx
end

local function disass(code, addr, out)
  create(code, addr, out):disass()
end

return {
  create = create,
  disass = disass,
  regname = regname,
}
