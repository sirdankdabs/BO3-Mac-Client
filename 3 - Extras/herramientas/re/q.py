import os
import pickle, bisect, collections, capstone, lief, sys
EXE = os.path.expanduser("~/Library/Application Support/Steam/steamapps/common/Call of Duty Black Ops III/CoDBlkOps3.app/Contents/MacOS/CoDBlkOps3_Exe")
IB = 0x100000000
_d = pickle.load(open(__file__.rsplit('/',1)[0]+'/idx.pkl','rb'))
starts = _d['starts']
refs_by_t = collections.defaultdict(list)
for a,f,t,m in _d['refs']: refs_by_t[t].append((a,f,m))
calls_by_t = collections.defaultdict(list); calls_by_f = collections.defaultdict(list)
for a,f,t,m in _d['calls']: calls_by_t[t].append((a,f)); calls_by_f[f].append((a,t))
refs_by_f = collections.defaultdict(list)
for a,f,t,m in _d['refs']: refs_by_f[f].append((a,t,m))
raw = open(EXE,'rb').read()
_b = lief.parse(EXE)
secs = [(s.virtual_address, s.offset, s.size, s.name) for s in _b.sections]
def v2o(v):
    for va,off,sz,n in secs:
        if va <= v < va+sz: return off + v - va
def o2v(o):
    for va,off,sz,n in secs:
        if off and off <= o < off+sz: return va + o - off
cs = _b.get_section("__cstring"); csoff, cssz = cs.offset, cs.size
def s(text, exact=True):
    """addresses of cstrings equal to (or containing) text"""
    out=[]; needle = text.encode(); blob = raw[csoff:csoff+cssz]; i = 0
    while True:
        i = blob.find(needle, i)
        if i < 0: break
        st = blob.rfind(b'\0',0,i)+1; en = blob.find(b'\0', i)
        if (not exact) or (st==i and en==i+len(needle)):
            out.append(o2v(csoff+i))
        i += 1
    return out
def cstr(v, n=200):
    o = v2o(v); 
    if o is None: return None
    e = raw.find(b"\0", o, o+n); e = o+n if e < 0 else e; return raw[o:e].decode("latin1")
def func_of(a):
    i = bisect.bisect_right(starts, a)-1; return starts[i]
def fend(f):
    i = bisect.bisect_right(starts, f); return starts[i] if i < len(starts) else f+0x1000
def xr(t): return refs_by_t.get(t, [])
def xfuncs(text, exact=True):
    r=set()
    for a in s(text, exact):
        for ia,f,m in xr(a): r.add(f)
    return sorted(r)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
def dis(f, n=200, end=None):
    end = end or fend(f); o = v2o(f)
    lines=[]
    for i in md.disasm(raw[o:o+(end-f)], f):
        c = ''
        if '[rip' in i.op_str:
            import re
            m = re.search(r'\[rip ([+-]) 0x([0-9a-f]+)\]', i.op_str)
            t = i.address+i.size+int(m.group(2),16)*(1 if m.group(1)=='+' else -1)
            st = cstr(t, 80) if t >= cs.virtual_address and t < cs.virtual_address+cssz else None
            c = f'  ; {hex(t-IB)}' + (f' "{st}"' if st is not None else '')
        lines.append(f'{i.address-IB:8x}: {i.mnemonic} {i.op_str}{c}')
        if len(lines) >= n: break
    return '\n'.join(lines)
def callers(f): return sorted(set(x[1] for x in calls_by_t.get(f, [])))
def callees(f): return [t for a,t in calls_by_f.get(f,[])]
def h(x): return hex(x-IB) if isinstance(x,int) else [hex(y-IB) for y in x]
def next_call(insn_addr, maxn=8):
    o = v2o(insn_addr)
    for k,i in enumerate(md.disasm(raw[o:o+120], insn_addr)):
        if i.mnemonic == 'call' and i.op_str.startswith('0x'): return int(i.op_str,16)
        if k>=maxn: return None
def key_calls(text, exact=True):
    c = collections.Counter()
    for a in s(text, exact):
        for ia,f,m in xr(a):
            t = next_call(ia)
            if t: c[t]+=1
    return [(hex(t-IB),n) for t,n in c.most_common()]
def strs_of(f, n=60):
    return [cstr(t,n) for a,t,m in refs_by_f[f] if cs.virtual_address<=t<cs.virtual_address+cssz]
def region(lo, hi):
    a=bisect.bisect_left(starts,IB+lo); b_=bisect.bisect_left(starts,IB+hi)
    for f in starts[a:b_]:
        print(hex(f-IB), 'sz',hex(fend(f)-f),'callers',len(callers(f)), 'callees',[hex(x-IB) for x in callees(f)][:5], strs_of(f)[:4])
def find_imm(patterns, maxhits=200):
    """linear scan of __text for instructions whose text matches any pattern (regex); returns (addr, text, next_call)"""
    import re
    rx = re.compile(patterns)
    out=[]
    t=_b.get_section("__text"); base=t.virtual_address; data=raw[t.offset:t.offset+t.size]
    for i,f in enumerate(starts):
        e=fend(f); o=f-base
        for ins in md.disasm_lite(data[o:o+(e-f)], f):
            txt=f'{ins[2]} {ins[3]}'
            if rx.search(txt):
                out.append((ins[0], txt, next_call(ins[0])))
                if len(out)>=maxhits: return out
    return out
def scan_funcs(pred):
    """pred(list_of_(addr,mn,op)) -> bool ; returns matching function starts"""
    t=_b.get_section("__text"); base=t.virtual_address; data=raw[t.offset:t.offset+t.size]
    out=[]
    for f in starts:
        e=fend(f); o=f-base
        ins=list(md.disasm_lite(data[o:o+(e-f)], f))
        if pred(ins): out.append(f)
    return out
