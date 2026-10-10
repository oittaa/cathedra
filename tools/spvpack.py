"""SPIR-V repacker for the 64k: a SMOL-V style re-encoding (varints, result-relative
IDs, op remapping, compact swizzles) split into 9 field streams (header, op+length,
type, result, relative ids, swizzles, varint operands, raw operands, constants). LZMA packs
this ~45% smaller than raw SPIR-V. Operand ids: 2*id+1 for absolute ids and
2*zigzag(result - id) for result-relative ones. The decoder takes either form anywhere, so the
encoder picks: absolute for module-level ids (types, constants, globals: id < first OpFunction,
so repeats match), call targets and references more than 64 back; relative otherwise.
OpExtInst always imports GLSL.std.450 as id 1 (not stored). A 32-bit OpConstant is a short
decimal when one gives the exact float (zigzag(n) * 16 + k: n / 10^k in float32, 10^k exact
for k <= 10), else 0 and the raw bits. Expects ids in definition order (tools/spv_renum.py). src/main.c has the
matching decoder (unspv). Debug info must already be stripped. Idea and opcode table:
SMOL-V by Aras Pranckevicius (MIT), github.com/aras-p/smol-v."""
import struct
from fractions import Fraction
from smol_ops import OPS
OPS = list(OPS)
OPS[245] = 1 | 2 | (31 << 3)   # OpPhi: every operand an id
OPS[254] = 1 << 3              # OpReturnValue: the value id
OPS[65] = 1 | 2 | (31 << 3)    # OpAccessChain: base and indices as ids
OPS[68] = 1 | 2 | 4 | (1 << 3) # OpArrayLength: the structure id, then the member
NOPS = len(OPS)
# one-byte opcode slots (greedy search on this module set); 12 = OpExtInst, 13 = compact swizzle
SWP = [80, 61, 62, 136, 131, 81, 245, 250, 248, 142, 133, 129, 12, 13, 57, 249]
SWAP = {}
for k, v in enumerate(SWP):
    if k != v: SWAP[v] = k; SWAP[k] = v
NS = 9   # streams: 0 hdr, 1 op, 2 type, 3 res, 4 rel, 5 swz, 6 var, 7 raw, 8 constants
FAR = 64
def flags(op): return OPS[op] if op < NOPS else 0
def vi(o, x):
    x &= 0xffffffff
    while x >= 128: o.append(x & 127 | 128); x >>= 7
    o.append(x)
def zz(d): d &= 0xffffffff; d = d - (1 << 32) if d >> 31 else d; return ((d << 1) ^ (d >> 31)) & 0xffffffff
def zd(u): return (~(u >> 1)) & 0xffffffff if u & 1 else u >> 1
def gbound(w):   # ids below the first OpFunction's result are module-level
    i = 5
    while i < len(w) and w[i] & 0xffff != 54: i += w[i] >> 16
    return w[i + 2] if i < len(w) else 1 << 31
def eid(prev, x, g, call): return x * 2 + 1 if x < g or call or prev - FAR > x else zz(prev - x) * 2
def did(prev, v): return v >> 1 if v & 1 else (prev - zd(v >> 1)) & 0xffffffff
def extra(op): return (4 if op == 79 else 0) + (3 if op in (61, 65) else 0) + (2 if op == 71 else 0)
def f32(x):   # float32 bits of a rational, correctly rounded (half to even), as x86 divss gives
    if x == 0: return 0
    s = 0
    if x < 0: s, x = 1, -x
    e = 0
    while x >= 2: x /= 2; e += 1
    while x < 1: x *= 2; e -= 1
    m = x * (1 << 23); mi = int(m); r = m - mi
    if r > Fraction(1, 2) or (r == Fraction(1, 2) and mi & 1): mi += 1
    if mi == 1 << 24: mi >>= 1; e += 1
    assert -126 <= e <= 127
    return s << 31 | (e + 127) << 23 | (mi - (1 << 23))
def dec(c):   # constant bits -> the decimal code, or 0 if no short decimal is exact
    if not c & 0x7fffffff or c >> 23 & 255 == 255: return 0
    v = struct.unpack("<f", struct.pack("<I", c))[0]
    for k in range(11):
        n = round(abs(v) * 10 ** k)
        if n >= 1 << 24: return 0
        n = -n if c >> 31 else n
        if n and f32(Fraction(n, 10 ** k)) == c: return zz(n) << 4 | k
    return 0
def undec(x): n = zd(x >> 4); return f32(Fraction(n - (1 << 32) if n >> 31 else n, 10 ** (x & 15)))
def encode_all(mods):
    S = [bytearray() for _ in range(NS)]
    for b in mods:
        w = struct.unpack("<%dI" % (len(b) // 4), b)
        g = gbound(w)
        S[0] += struct.pack("<5I", len(w) - 5, *w[1:5])
        i, prev = 5, 0
        while i < len(w):
            n, op = w[i] >> 16, w[i] & 0xffff
            ins = w[i:i + n]; i += n
            swz = None
            if op == 79 and n <= 9 and all(x < 4 for x in ins[5:]): swz = sum(x << (6 - 2 * k) for k, x in enumerate(ins[5:])); eop = 13
            else: eop = op
            L = n - 1 - extra(op)
            assert 0 <= L < 0x10000
            r = SWAP.get(eop, eop)
            vi(S[1], ((L >> 4) << 20) | ((r >> 4) << 8) | ((L & 15) << 4) | (r & 15))
            f = flags(op); k = 1
            if f & 2: vi(S[2], ins[k]); k += 1
            if f & 1: vi(S[3], zz(ins[k] - prev)); prev = ins[k]; k += 1
            if op == 43 and n == 4:   # 32-bit constant
                x = dec(ins[3]); vi(S[8], x)
                if not x: S[7] += struct.pack("<I", ins[3])
                continue
            if op == 12: assert ins[k] == 1; vi(S[6], ins[k + 1]); k += 2; f |= 31 << 3   # OpExtInst: GLSL.std.450 opcode, then ids
            for j in range(f >> 3):
                if k >= n: break
                vi(S[4], eid(prev, ins[k], g, op == 57 and not j)); k += 1
            if swz is not None: S[5].append(swz)
            elif f & 4:
                for x in ins[k:]: vi(S[6], x)
            else: S[7] += struct.pack("<%dI" % (n - k), *ins[k:])
    ofs, o = [], 0
    for s in S: ofs.append(o); o += len(s)
    return b"".join(bytes(s) for s in S), ofs
def decode_all(blob, ofs, nmods):
    P = list(ofs)
    def rv(k):
        v = s = 0
        while True:
            c = blob[P[k]]; P[k] += 1; v |= (c & 127) << s; s += 7
            if not c & 128: return v
    def r4(k): v = struct.unpack_from("<I", blob, P[k])[0]; P[k] += 4; return v
    mods = []
    for _ in range(nmods):
        nw = r4(0); out = [0x07230203] + [r4(0) for _ in range(4)]; prev = 0
        while len(out) < nw + 5:
            x = rv(1); L = ((x >> 20) << 4) | ((x >> 4) & 15); op = ((x >> 4) & 0xfff0) | (x & 15)
            if op < 16: op = SWP[op]
            else:
                for j in range(16):
                    if SWP[j] == op: op = j
            swz = op == 13
            if swz: op = 79
            n = L + 1 + extra(op); ins = [n << 16 | op]; f = flags(op)
            if f & 2: ins.append(rv(2))
            if f & 1: prev = (prev + zd(rv(3))) & 0xffffffff; ins.append(prev)
            if op == 43 and n == 4:
                x = rv(8); ins.append(undec(x) if x else r4(7)); out += ins; continue
            if op == 12: ins.append(1); ins.append(rv(6)); f |= 31 << 3
            for _ in range(f >> 3):
                if len(ins) >= n: break
                ins.append(did(prev, rv(4)))
            if swz:
                c = blob[P[5]]; P[5] += 1
                while len(ins) < n: ins.append((c >> (6 - 2 * (len(ins) - 5))) & 3)
            elif f & 4:
                while len(ins) < n: ins.append(rv(6))
            else:
                while len(ins) < n: ins.append(r4(7))
            out += ins
        mods.append(struct.pack("<%dI" % len(out), *out))
    return mods
