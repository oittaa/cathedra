#!/usr/bin/env python3
"""SPIR-V size pass: turn read-only pointer parameters (GLSL 'in' params, which glslang
passes by pointer through a temp variable at every call site) into by-value parameters.
Usage: spv_byval.py in.spv out.spv   (run spirv-opt afterwards so the temps get promoted)"""
import struct, sys

LOAD, ACHAIN, CALL, FUNC, FPARAM, TFUNC, TPTR, CONST, STORE, COPY, CEXT, VEXTD = 61, 65, 57, 54, 55, 33, 32, 43, 62, 83, 81, 77
FUNCEND = 56
OPAQUE = {25, 26, 27, 4472, 5341, 19}  # image, sampler, sampled image, ray query, accel struct, void

def parse(ws):
    out, i = [], 5
    while i < len(ws):
        n = ws[i] >> 16
        out.append([ws[i] & 0xffff] + list(ws[i + 1:i + n]))
        i += n
    return out

def one_round(hdr, ins):
    ptr, typeop, consts, ftypes, tdefs = {}, {}, {}, {}, {}
    for I in ins:
        op = I[0]
        if op == TPTR: ptr[I[1]] = (I[2], I[3])
        if op in (19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 4472, 5341): typeop[I[1]] = I
        if op == CONST: consts[I[2]] = I[3]
        if op == TFUNC: ftypes[tuple(I[2:])] = I[1]; tdefs[I[1]] = I
    def ok_type(t):
        if t not in ptr or ptr[t][0] != 7: return False   # Function storage class only
        T = ptr[t][1]
        return typeop.get(T, [0])[0] not in OPAQUE and typeop.get(T, [0])[0] not in (30,)  # no structs
    # collect functions
    funcs, cur = {}, None
    for idx, I in enumerate(ins):
        if I[0] == FUNC: cur = I[2]; funcs[cur] = {"start": idx, "params": []}
        elif I[0] == FPARAM and cur is not None: funcs[cur]["params"].append(I[2])
        elif I[0] == FUNCEND: funcs[cur]["end"] = idx; cur = None
    # entry points must keep their signature (they have no params anyway)
    convert = {}  # param id -> pointee type
    for f, F in funcs.items():
        body = ins[F["start"]:F["end"]]
        ptypes = {I[2]: I[1] for I in body if I[0] == FPARAM}
        for p in F["params"]:
            if not ok_type(ptypes[p]): continue
            good, chains = True, set()
            for I in body:
                if I[0] == FPARAM: continue
                ids = I[1:]
                if I[0] == LOAD and I[3] == p: continue
                if I[0] == ACHAIN and I[3] == p:
                    if all(x in consts for x in I[4:]) or (len(I) == 5 and typeop.get(ptr[ptypes[p]][1], [0])[0] == 23):
                        chains.add(I[2]); continue
                    good = False; break
                if I[0] == LOAD and I[3] in chains: continue
                if p in ids or any(c in ids for c in chains): good = False; break
            if good: convert[p] = ptr[ptypes[p]][1]
    if not convert: return hdr, ins, 0
    bound = hdr[3]
    def newid():
        nonlocal bound
        bound += 1
        return bound - 1
    out, cur, ptypes_all = [], None, {}
    new_types = []
    # function type rewrites
    fsig = {}
    for f, F in funcs.items():
        FI = ins[F["start"]]
        old = tdefs[FI[4]]
        params = [convert.get(p) for p in F["params"]]
        if not any(params): continue
        pt = [params[k] if params[k] else old[3 + k] for k in range(len(F["params"]))]
        key = tuple([old[2]] + pt)
        if key not in ftypes:
            nid = newid(); ftypes[key] = nid; new_types.append([TFUNC, nid] + list(key))
        fsig[f] = (ftypes[key], [bool(x) for x in params])
    chainmap = {}  # chain result id -> value id
    for I in ins:
        op = I[0]
        if op == FUNC and I[2] in fsig:
            I = I[:]; I[4] = fsig[I[2]][0]
        elif op == FPARAM and I[2] in convert:
            I = [FPARAM, convert[I[2]], I[2]]
        elif op == LOAD and I[3] in convert:
            I = [COPY, I[1], I[2], I[3]]
        elif op == ACHAIN and I[3] in convert:
            base = I[3]; T = ptr[I[1]][1]
            if all(x in consts for x in I[4:]):
                I = [CEXT, T, I[2], base] + [consts[x] for x in I[4:]]
            else:
                I = [VEXTD, T, I[2], base, I[4]]
            chainmap[I[2]] = 1
        elif op == LOAD and I[3] in chainmap:
            I = [COPY, I[1], I[2], I[3]]
        elif op == CALL and I[3] in fsig:
            flags = fsig[I[3]][1]; args = I[4:]; newargs = []
            for k, a in enumerate(args):
                if flags[k]:
                    v = newid(); T = convert[funcs[I[3]]["params"][k]]
                    out.append([LOAD, T, v, a]); newargs.append(v)
                else: newargs.append(a)
            I = I[:4] + newargs
        out.append(I)
        if op == TFUNC and new_types:
            pass
    # insert new function types right after the last existing OpTypeFunction/type decl before first OpFunction
    first_func = next(i for i, I in enumerate(out) if I[0] == FUNC)
    # place before first constant/variable that might come later? types only need their operand types defined earlier:
    # put them just after the last OpTypePointer / OpTypeFunction among globals
    last = max(i for i, I in enumerate(out[:first_func]) if I[0] in (TPTR, TFUNC))
    out[last + 1:last + 1] = new_types
    hdr = hdr[:]; hdr[3] = bound
    return hdr, out, len(convert)

def run(data):
    ws = list(struct.unpack("<%dI" % (len(data) // 4), data))
    hdr, ins = ws[:5], parse(ws)
    total = 0
    while True:
        hdr, ins, n = one_round(hdr, ins)
        total += n
        if not n: break
    o = hdr[:]
    for I in ins: o += [(len(I) << 16) | I[0]] + I[1:]
    return struct.pack("<%dI" % len(o), *o), total

if __name__ == "__main__":
    d, n = run(open(sys.argv[1], "rb").read())
    open(sys.argv[2], "wb").write(d)
    print("byval: %d params" % n, file=sys.stderr)
