import os
import lief, capstone, pickle, sys, re, bisect
EXE = os.path.expanduser("~/Library/Application Support/Steam/steamapps/common/Call of Duty Black Ops III/CoDBlkOps3.app/Contents/MacOS/CoDBlkOps3_Exe")
b = lief.parse(EXE)
text = b.get_section("__text")
data = bytes(text.content); base = text.virtual_address
starts = sorted(set(b.imagebase+f for f in b.function_starts.functions if base <= b.imagebase+f < base+len(data)))
print("functions", len(starts), file=sys.stderr)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
riprx = re.compile(r'\[rip ([+-]) 0x([0-9a-f]+)\]')
refs = []   # (insn, func, target, mnem)
calls = []  # (insn, func, target)
ends = starts[1:] + [base+len(data)]
for i,(s,e) in enumerate(zip(starts, ends)):
    chunk = data[s-base:e-base]
    pos = 0
    while pos < len(chunk):
      last = pos
      for addr, size, mn, op in md.disasm_lite(chunk[pos:], s+pos):
        last = addr - s + size
        if mn == 'call' or mn == 'jmp':
            if op.startswith('0x'):
                calls.append((addr, s, int(op,16), mn))
        m = riprx.search(op)
        if m:
            off = int(m.group(2),16) * (1 if m.group(1)=='+' else -1)
            refs.append((addr, s, addr+size+off, mn))
      pos = last + 1 if last == pos else last
    if i % 20000 == 0: print(i, file=sys.stderr)
pickle.dump({'starts':starts,'refs':refs,'calls':calls}, open(sys.argv[1],'wb'))
print("refs", len(refs), "calls", len(calls), file=sys.stderr)
