#!/usr/bin/env python3
"""Tests for the production host-I/O policy checker."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

from tools import check_host_io


class CheckHostIoTest(unittest.TestCase):
    def test_rejects_unannotated_stream(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sample.cc"
            path.write_text("void f() { std::ifstream input(\"x\"); }\n", encoding="utf-8")
            self.assertEqual(len(check_host_io.check(path)), 1)

    def test_accepts_reason_marker(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sample.cc"
            path.write_text("// XFF_HOST_IO: adapter\nstd::ifstream input(\"x\");\n", encoding="utf-8")
            self.assertFalse(check_host_io.check(path))

    def test_ignores_tests(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sample_test.cc"
            path.write_text("std::ifstream input(\"x\");\n", encoding="utf-8")
            self.assertFalse(check_host_io.check(path))

    def test_rejects_unannotated_filesystem_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sample.cc"
            path.write_text("void f() { std::filesystem::remove(\"x\"); }\n", encoding="utf-8")
            self.assertEqual(len(check_host_io.check(path)), 1)

    def test_annotation_cannot_bypass_mutation_routing(self) -> None:
        examples = [
            'std::ofstream output("x");',
            'stdfs::remove_all("x", error);',
            '::unlink("x");',
            '::open("x", O_WRONLY | O_CREAT, 0600);',
            'archive_write_open_filename(writer, "x");',
            '::write(fd, data, size);',
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sample.cc"
            for text in examples:
                with self.subTest(text=text):
                    path.write_text("// XFF_HOST_IO: attempted bypass\n" + text + "\n", encoding="utf-8")
                    self.assertEqual(len(check_host_io.check(path)), 1)

    def test_accepts_annotated_filesystem_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sample.cc"
            path.write_text("// XFF_HOST_IO: adapter\nstdfs::status(\"x\", error);\n", encoding="utf-8")
            self.assertFalse(check_host_io.check(path))

    def test_rejects_unannotated_posix_metadata_probe(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sample.cc"
            path.write_text("void f() { ::access(\"x\", W_OK); }\n", encoding="utf-8")
            self.assertEqual(len(check_host_io.check(path)), 1)


if __name__ == "__main__":
    unittest.main()
