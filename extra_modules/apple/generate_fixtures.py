#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Generate tiny, independently encoded package fixtures using only Python's standard library."""
import io
import zipfile
import gzip
import hashlib
import lzma
from pathlib import Path
import struct
import zlib


def cpio(name, data):
    def entry(path, content, inode):
        fields = (inode, 0o100644, 0, 0, 1, 0, len(content), 0, 0, 0, 0, len(path) + 1, 0)
        header = b"070701" + b"".join(f"{value:08x}".encode() for value in fields)
        header += path.encode() + b"\0"
        header += b"\0" * (-len(header) % 4)
        return header + content + b"\0" * (-len(content) % 4)
    return entry(name, data, 1) + entry("TRAILER!!!", b"", 2)


def pbzx(data, raw=False):
    chunks = [data[:len(data)//2], data[len(data)//2:]]
    output = b"pbzx" + struct.pack(">Q", 16 * 1024 * 1024)
    for index, chunk in enumerate(chunks):
        stored = chunk if raw or index == 0 else lzma.compress(chunk)
        output += struct.pack(">QQ", len(chunk), len(stored)) + stored
    return output


def xar(name, data):
    digest = hashlib.sha1(data).hexdigest()
    toc = (f'<?xml version="1.0"?><xar><toc><checksum style="sha1"><offset>0</offset>'
           f'<size>20</size></checksum><file id="1"><name>{name}</name><type>file</type>'
           f'<mode>0644</mode><data><length>{len(data)}</length><offset>20</offset>'
           f'<size>{len(data)}</size><encoding style="application/octet-stream"/>'
           f'<archived-checksum style="sha1">{digest}</archived-checksum>'
           f'<extracted-checksum style="sha1">{digest}</extracted-checksum>'
           '</data></file></toc></xar>').encode()
    compressed = zlib.compress(toc)
    return struct.pack(">4sHHQQI", b"xar!", 28, 1, len(compressed), len(toc), 1) + compressed + hashlib.sha1(compressed).digest() + data


def main():
    target = Path(__file__).with_name("test_data")
    target.mkdir(exist_ok=True)
    payload = cpio("Applications/Example.app/Contents/info.txt", b"portable package\n")
    files = {
        "payload.cpio": payload,
        "payload.pbzx": pbzx(payload),
        "raw.pbzx": pbzx(payload, raw=True),
        "plain.pkg": xar("Payload", gzip.compress(payload, mtime=0)),
        "pbzx.pkg": xar("Payload", pbzx(payload)),
        "sample.xip": xar("Content", pbzx(payload)),
        "sample.xar": xar("hello.txt", b"hello\n"),
    }
    compressed = lzma.compress(b"hello")
    files["xz.pbzx"] = b"pbzx" + struct.pack(">QQQ", 16, 5, len(compressed)) + compressed
    # LZMA2 dictionary property 32 requests 256 MiB. Recompute the XZ block-header CRC;
    # no large dictionary is allocated when constructing this memory-limit fixture.
    oversized = bytearray(compressed)
    oversized[16] = 32
    oversized[20:24] = struct.pack("<I", zlib.crc32(oversized[12:20]))
    files["memory-limit.pbzx"] = b"pbzx" + struct.pack(">QQQ", 16, 5, len(oversized)) + oversized
    zip_bytes = io.BytesIO()
    with zipfile.ZipFile(zip_bytes, "w", zipfile.ZIP_DEFLATED) as archive:
        entry = zipfile.ZipInfo("Applications/Example.app/Contents/info.txt", (2020, 1, 1, 0, 0, 0))
        archive.writestr(entry, b"portable package\n")
    for extension in ("zip", "ipa", "ipsw"):
        files["application." + extension] = zip_bytes.getvalue()
    files["nested.pkg"] = xar("component.pkg", files["plain.pkg"])
    for name, contents in files.items():
        (target / name).write_bytes(contents)


if __name__ == "__main__":
    main()
