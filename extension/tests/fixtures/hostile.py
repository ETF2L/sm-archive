#!/usr/bin/env python3
# Copyright 2026 ETF2L Contributors, Dave
#
# Writes hostile and odd archives for the unit tests into DIR/hostile/:
#   python3 hostile.py DIR
# Each file is named <ArchiveError>[.<ArchiveError>]-<what>.<ext>: the error
# that extracting it must end with (None for success) and, where it differs,
# the one listing it must end with. Listing writes nothing, so it cannot
# tell link collisions, and of the data it only counts what a codec decodes
# for a tar: it tells a compressed tar's bomb, not a zip's. Python's zipfile
# and tarfile write exactly the names and types given, which is what the
# tests need.
import io
import os
import stat
import sys
import tarfile
import zipfile


def zip_file(path, entries, method=zipfile.ZIP_DEFLATED):
    """entries: (name, data, external_attr or None)"""
    with zipfile.ZipFile(path, "w", method) as archive:
        for name, data, attributes in entries:
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 21, 12, 0, 0))
            info.compress_type = method
            if attributes is not None:
                info.external_attr = attributes
            archive.writestr(info, data)


def tar_file(path, members, mode="w"):
    """members: (name, type, data, linkname)"""
    with tarfile.open(path, mode, format=tarfile.PAX_FORMAT) as archive:
        for name, kind, data, linkname in members:
            info = tarfile.TarInfo(name)
            info.type = kind
            info.mtime = 1700000000
            info.linkname = linkname
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data) if data else None)


def cp437_zip(path):
    # zipfile stores a name that cp437 can encode as cp437 without the UTF-8
    # flag: "café" becomes the byte 0x82, which is no UTF-8.
    zip_file(path, [("café.txt", b"x", None)])


def encrypted_zip(path):
    # A normal zip whose entry is flagged encrypted (bit 0) in its local and
    # central headers: what matters is that it is refused, not decrypted.
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("secret.txt", b"secret")
    data = bytearray(buffer.getvalue())
    for signature, offset in ((b"PK\x03\x04", 6), (b"PK\x01\x02", 8)):
        position = data.find(signature)
        data[position + offset] |= 0x01
    open(path, "wb").write(data)


def main():
    out = os.path.join(sys.argv[1], "hostile")
    os.makedirs(out, exist_ok=True)

    def at(name):
        return os.path.join(out, name)

    zip_file(at("UnsafeEntry-zip-slip.zip"), [("ok.txt", b"ok", None), ("../../evil.txt", b"evil", None)])
    zip_file(at("UnsafeEntry-zip-absolute.zip"), [("/tmp/evil.txt", b"evil", None)])
    zip_file(at("UnsafeEntry-zip-backslash-slip.zip"), [("..\\..\\evil.txt", b"evil", None)])
    zip_file(at("UnsafeEntry-zip-duplicate.zip"), [("a.txt", b"1", None), ("a.txt", b"2", None)])
    zip_file(at("None-zip-backslash.zip"), [("addons\\sourcemod\\x.smx", b"smx", None)])
    symlink_attributes = (stat.S_IFLNK | 0o777) << 16
    zip_file(at("None-zip-symlink-out.zip"), [("link", b"../../../etc/passwd", symlink_attributes),
                                             ("file.txt", b"file", None)])
    cp437_zip(at("EntryName-zip-cp437.zip"))
    encrypted_zip(at("Encrypted-zip-flagged.zip"))
    # Deflate cannot pack more than about 1032:1, bzip2 and xz far more.
    zip_file(at("LimitRatio.None-zip-bomb.zip"), [("zeros", b"\0" * (48 * 1024 * 1024), None)], zipfile.ZIP_BZIP2)
    many = [("f%05d" % i, b"", None) for i in range(10001)]
    zip_file(at("LimitEntries-zip-many.zip"), many)

    tar_file(at("UnsafeEntry-tar-slip.tar"), [("../evil.txt", tarfile.REGTYPE, b"evil", "")])
    tar_file(at("UnsafeEntry-tar-absolute.tar.gz"), [("/etc/evil", tarfile.REGTYPE, b"evil", "")], "w:gz")
    tar_file(at("None-tar-links.tar.xz"), [
        ("dir/file.txt", tarfile.REGTYPE, b"file", ""),
        ("inside", tarfile.SYMTYPE, b"", "dir/file.txt"),
        ("outside", tarfile.SYMTYPE, b"", "../../outside"),
        ("hard", tarfile.LNKTYPE, b"", "dir/file.txt"),
        ("hardout", tarfile.LNKTYPE, b"", "../outside"),
        ("device", tarfile.CHRTYPE, b"", ""),
        ("fifo", tarfile.FIFOTYPE, b"", ""),
    ], "w:xz")
    tar_file(at("UnsafeEntry.None-tar-link-then-file.tar.bz2"), [
        ("real/x", tarfile.REGTYPE, b"x", ""),
        ("alias", tarfile.SYMTYPE, b"", "real"),
        ("alias/x", tarfile.REGTYPE, b"again", ""),
    ], "w:bz2")
    # Each link stays inside when it is created; "a" makes "esc" the
    # target's parent afterwards.
    tar_file(at("UnsafeEntry.None-tar-link-retargeted.tar"), [
        ("esc", tarfile.SYMTYPE, b"", "a/.."),
        ("a", tarfile.SYMTYPE, b"", "."),
    ])
    tar_file(at("LimitRatio-tar-bomb.tar.xz"), [("zeros", tarfile.REGTYPE, b"\0" * (48 * 1024 * 1024), "")], "w:xz")
    open(at("UnknownFormat-garbage.tar"), "wb").write(b"no archive at all " * 100)
    with open(at("None-v7.tar"), "wb") as handle:
        with tarfile.open(fileobj=handle, mode="w", format=tarfile.USTAR_FORMAT) as archive:
            info = tarfile.TarInfo("old.txt")
            info.size = 3
            archive.addfile(info, io.BytesIO(b"old"))
    # A v7 tar has no "ustar" magic; blank it out of the ustar file above.
    data = bytearray(open(at("None-v7.tar"), "rb").read())
    data[257:265] = b"\0" * 8
    checksum = sum(data[0:148]) + 8 * 32 + sum(data[156:512])
    data[148:156] = b"%06o\0 " % checksum
    open(at("None-v7.tar"), "wb").write(data)


if __name__ == "__main__":
    main()
