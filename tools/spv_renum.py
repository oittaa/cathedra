#!/usr/bin/env python3
"""SPIR-V size pass: renumber every id in order of definition (1, 2, 3, ...).
spirv-opt --compact-ids numbers ids in order of first *mention* (forward refs from
OpEntryPoint/OpDecorate/branches scramble it); in definition order every result id is the
previous one + 1, so spvpack's result-delta stream becomes constant and operand distances
shrink and repeat (~7% smaller after LZMA). Round-trips through spirv-dis/spirv-as and
checks that the disassembly is unchanged apart from the id names.
Usage: spv_renum.py in.spv out.spv"""
import os, re, struct, subprocess, sys, tempfile

def dis(path):
    return subprocess.run(["spirv-dis", "--raw-id", "--no-header", "--no-color", path],
                          capture_output=True, text=True, check=True).stdout

def run(data):
    d = tempfile.mkdtemp()
    a, s, b = os.path.join(d, "a.spv"), os.path.join(d, "b.spvasm"), os.path.join(d, "b.spv")
    open(a, "wb").write(data)
    t = dis(a)
    defs = re.findall(r"^\s*%(\d+) = ", t, re.M)
    assert len(defs) == len(set(defs))
    mp = {x: str(i + 1) for i, x in enumerate(defs)}
    t2 = re.sub(r"%(\d+)\b", lambda g: "%" + mp[g.group(1)], t)
    open(s, "w").write(t2)
    subprocess.run(["spirv-as", "--target-env", "vulkan1.3", "--preserve-numeric-ids", s, "-o", b], check=True)
    out = data[:12] + open(b, "rb").read()[12:]   # keep the original generator word
    open(b, "wb").write(out)
    norm = lambda x: [l.split() for l in x.splitlines()]
    assert norm(dis(b)) == norm(t2), "spirv-dis/spirv-as round trip changed the module"
    # binary check: same instructions word for word, every changed word is a remapped id
    wa, wb = (struct.unpack("<%dI" % (len(x) // 4), x) for x in (data, out))
    imap = {int(k): int(v) for k, v in mp.items()}
    assert len(wa) == len(wb) and wa[:3] == wb[:3] and wb[3] == len(defs) + 1
    assert all(x == y or imap.get(x) == y for x, y in zip(wa[5:], wb[5:])), "renumber changed a literal"
    for f in (a, s, b): os.remove(f)
    os.rmdir(d)
    return out

if __name__ == "__main__":
    open(sys.argv[2], "wb").write(run(open(sys.argv[1], "rb").read()))
