#!/usr/bin/env python3
"""SPIR-V size pass: reorder the type / constant / global-variable section so that the ids
used most often as operands come first. A topological sort (every definition still precedes
its uses) with a priority: ids referenced 8+ times by operand count, then the rest in two
groups (2-7 uses, fewer), constants sorted by value inside a group. Run before
tools/spv_renum.py: in definition order the hot constants get the smallest ids, which spvpack
writes in one byte (ids < 64), and the constant values come out nearly sorted (~450 bytes
smaller after LZMA). Checks that only the order of the declaration lines changed.
Usage: spv_sortdecl.py in.spv out.spv"""
import heapq, os, re, subprocess, sys, tempfile
from collections import Counter, defaultdict

DECL = re.compile(r"^\s*%(\d+) = Op(Type\w*|Constant\w*|SpecConstant\w*|Variable|Undef)\b")
REF = re.compile(r"%(\d+)\b")
UNTYPED = re.compile(r"= Op(Type\w*|Label|ExtInstImport|String)\b")   # results without a result type

def run(data):
    d = tempfile.mkdtemp()
    a, s, b = os.path.join(d, "a.spv"), os.path.join(d, "b.spvasm"), os.path.join(d, "b.spv")
    open(a, "wb").write(data)
    L = subprocess.run(["spirv-dis", "--raw-id", "--no-header", "--no-color", a],
                       capture_output=True, text=True, check=True).stdout.splitlines()
    ff = next(i for i, l in enumerate(L) if " = OpFunction " in l)
    ds = next(i for i in range(ff) if DECL.match(L[i]))
    decl = L[ds:ff]
    assert all(DECL.match(l) for l in decl), "unexpected instruction in the declaration section"
    rid = [int(DECL.match(l).group(1)) for l in decl]
    pos = {r: k for k, r in enumerate(rid)}
    # uses as an operand (the result-type slot does not count: spvpack writes types in their own stream)
    use = Counter()
    for l in L:
        m = re.match(r"^\s*%\d+ = Op\w+(.*)$", l)
        refs = [int(x) for x in REF.findall(m.group(1) if m else l)]
        if m and refs and not UNTYPED.search(l): refs = refs[1:]
        use.update(refs)
    deps, users = {}, defaultdict(list)
    for l, r in zip(decl, rid):
        deps[r] = {int(x) for x in REF.findall(l.split("=", 1)[1]) if int(x) in pos and int(x) != r}
        for x in deps[r]: users[x].append(r)
    def prio(r):
        l, u = decl[pos[r]], use[r]
        m = re.search(r"= OpConstant %\d+ (\S+)$", l)
        try: v = float(m.group(1)) if m else 0.
        except ValueError: v = 0.
        g = 0 if u >= 8 else 1 if u >= 2 else 2
        return (g, -u if g == 0 else 0, v, pos[r])
    left = {r: len(deps[r]) for r in rid}
    h = [(prio(r), r) for r in rid if not left[r]]
    heapq.heapify(h); order = []
    while h:
        r = heapq.heappop(h)[1]; order.append(r)
        for u in users[r]:
            left[u] -= 1
            if not left[u]: heapq.heappush(h, (prio(u), u))
    assert sorted(order) == sorted(rid), "dependency cycle in the declaration section"
    L2 = L[:ds] + [decl[pos[r]] for r in order] + L[ff:]
    open(s, "w").write("\n".join(L2) + "\n")
    subprocess.run(["spirv-as", "--target-env", "vulkan1.3", "--preserve-numeric-ids", s, "-o", b], check=True)
    out = data[:12] + open(b, "rb").read()[12:]   # keep the original generator word
    chk = subprocess.run(["spirv-dis", "--raw-id", "--no-header", "--no-color", "-"], input=out,
                         capture_output=True, check=True).stdout.decode().splitlines()
    assert chk == L2, "spirv-dis/spirv-as round trip changed the module"
    for f in (a, s, b): os.remove(f)
    os.rmdir(d)
    return out

if __name__ == "__main__":
    open(sys.argv[2], "wb").write(run(open(sys.argv[1], "rb").read()))
