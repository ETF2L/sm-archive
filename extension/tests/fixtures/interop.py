#!/usr/bin/env python3
# Copyright 2026 ETF2L Contributors, Dave
#
# Interop with the real tools (gzip, bzip2, xz, zstd, lz4, brotli, tar, zip,
# 7z), both ways:
#   python3 interop.py prepare DIR   the tools pack DIR/sample.bin and DIR/tree/
#                                    into DIR/tool/, and write encrypted
#                                    archives into DIR/hostile/
#   (the unit tests unpack DIR/tool/ and pack the same into DIR/ours/<bits>/)
#   python3 interop.py verify DIR    the tools unpack DIR/ours/*/ and compare
import filecmp
import os
import random
import shutil
import subprocess
import sys
import tempfile

CODECS = {
    "gz": (["gzip", "-c"], ["gzip", "-dc"]),
    "bz2": (["bzip2", "-c"], ["bzip2", "-dc"]),
    "xz": (["xz", "-c"], ["xz", "-dc"]),
    "zst": (["zstd", "-q", "-c"], ["zstd", "-q", "-dc"]),
    "lz4": (["lz4", "-q", "-c"], ["lz4", "-q", "-dc"]),
    "br": (["brotli", "-c"], ["brotli", "-dc"]),
}

TAR_PROGRAMS = {"gz": "gzip", "bz2": "bzip2", "xz": "xz", "zst": "zstd", "lz4": "lz4", "br": "brotli"}


def run(command, **kwargs):
    subprocess.run(command, check=True, **kwargs)


def pipe(command, source, target):
    with open(source, "rb") as given, open(target, "wb") as written:
        run(command, stdin=given, stdout=written)


def make_tree(root):
    os.makedirs(os.path.join(root, "addons/sourcemod/plugins"))
    os.makedirs(os.path.join(root, "empty"))
    with open(os.path.join(root, "addons/sourcemod/plugins/x.smx"), "wb") as handle:
        handle.write(bytes(random.Random(1).getrandbits(8) for _ in range(5000)))
    with open(os.path.join(root, "README.md"), "w") as handle:
        handle.write("readme\n" * 100)
    script = os.path.join(root, "run.sh")
    with open(script, "w") as handle:
        handle.write("#!/bin/sh\n")
    os.chmod(script, 0o755)


def prepare(directory):
    tool = os.path.join(directory, "tool")
    hostile = os.path.join(directory, "hostile")
    os.makedirs(tool)
    os.makedirs(hostile, exist_ok=True)
    sample = os.path.join(directory, "sample.bin")
    generator = random.Random(2)
    with open(sample, "wb") as handle:
        handle.write(b"".join(b"line %d of the sample\n" % i for i in range(15000)))
        handle.write(bytes(generator.getrandbits(8) for _ in range(100000)))
    tree = os.path.join(directory, "tree")
    make_tree(tree)

    for extension, (compress, _) in CODECS.items():
        pipe(compress, sample, os.path.join(tool, "sample.bin." + extension))
    run(["tar", "-cf", os.path.join(tool, "tree.tar"), "-C", tree, "."])
    for extension, program in TAR_PROGRAMS.items():
        run(["tar", "-I", program, "-cf", os.path.join(tool, "tree.tar." + extension), "-C", tree, "."])
    run(["zip", "-q", "-r", "-X", os.path.join(tool, "tree.zip"), "."], cwd=tree)
    run(["7z", "a", "-bd", "-bso0", os.path.join(tool, "tree.7z"), "."], cwd=tree)

    run(["zip", "-q", "-P", "secret", os.path.join(hostile, "Encrypted-zipcrypto.zip"), "README.md"], cwd=tree)
    run(["7z", "a", "-bd", "-bso0", "-psecret", os.path.join(hostile, "Encrypted-7z.7z"), "README.md"], cwd=tree)
    run(["7z", "a", "-bd", "-bso0", "-psecret", "-mhe=on", os.path.join(hostile, "Encrypted-7z-headers.7z"),
         "README.md"], cwd=tree)


def same_tree(expected, actual):
    comparison = filecmp.dircmp(expected, actual)
    if comparison.left_only or comparison.right_only or comparison.diff_files or comparison.funny_files:
        return False
    if any(not filecmp.cmp(os.path.join(expected, name), os.path.join(actual, name), shallow=False)
           for name in comparison.common_files):
        return False
    return all(same_tree(os.path.join(expected, name), os.path.join(actual, name)) for name in comparison.common_dirs)


def unpack_tree(archive, target):
    name = os.path.basename(archive)
    if name.endswith(".zip"):
        run(["unzip", "-q", archive, "-d", target])
    elif name.endswith(".7z"):
        run(["7z", "x", "-bd", "-bso0", "-o" + target, archive])
    else:
        extension = name.rsplit(".", 1)[1]
        command = ["tar", "-xf", archive, "-C", target]
        if extension in TAR_PROGRAMS:
            command[1:1] = ["-I", TAR_PROGRAMS[extension]]
        run(command)


def verify(directory):
    sample = os.path.join(directory, "sample.bin")
    tree = os.path.join(directory, "tree")
    failures = 0
    checked = 0
    ours = os.path.join(directory, "ours")
    for bits in sorted(os.listdir(ours)):
        for name in sorted(os.listdir(os.path.join(ours, bits))):
            path = os.path.join(ours, bits, name)
            checked += 1
            if name.startswith("sample.bin."):
                with tempfile.NamedTemporaryFile() as unpacked:
                    pipe(CODECS[name.rsplit(".", 1)[1]][1], path, unpacked.name)
                    is_same = filecmp.cmp(sample, unpacked.name, shallow=False)
            else:
                target = tempfile.mkdtemp()
                unpack_tree(path, target)
                is_same = same_tree(tree, target)
                shutil.rmtree(target)
            if not is_same:
                print("interop: %s/%s differs after unpacking with the tools" % (bits, name))
                failures += 1
    print("interop: %d file(s) checked with the tools, %d failed" % (checked, failures))
    return failures == 0 and checked > 0


def main():
    action, directory = sys.argv[1], sys.argv[2]
    if action == "prepare":
        prepare(directory)
        return 0
    return 0 if verify(directory) else 1


if __name__ == "__main__":
    sys.exit(main())
