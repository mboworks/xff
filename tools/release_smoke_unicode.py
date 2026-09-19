# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Unicode-only staged-executable smoke fixtures; ordinary smoke data stays ASCII."""

import csv
import io
import json


# Unicode-specific smoke checks use their own fixture and never alter general scenarios.
def unicode_smoke(run, root, check):
    directory = root / "unicode"
    directory.mkdir()
    name = "café.txt"  # Latin e with acute: multibyte filename round trip.
    (directory / name).write_text("unicode filename\n", encoding="utf-8")
    records = [json.loads(line) for line in run(directory, "-type", "f", "--format=jsonl").splitlines()]
    check(records == [{"path": str(directory / name)}], "Unicode JSONL filename round trip")
    rows = list(csv.DictReader(io.StringIO(run(directory, "-type", "f", "--format=csv", "--columns=name"))))
    check(rows == [{"name": name}], "Unicode CSV filename round trip")
    check(run(directory, "-type", "f", "--format=nul") == str(directory / name) + "\0",
          "Unicode NUL filename round trip")
