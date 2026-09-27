<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Language database build extension

This removable extension embeds the MIT-licensed GitHub Linguist `languages.yml` vocabulary from
version 9.6.0. The source is retained for auditability; the binary links a 17,637-byte Brotli payload
instead of the 109,261-byte compact JSON and decompresses it only on first use.

Regenerate the payload with:

```bash
tools/generate_language_db.rb extra_modules/language_db/data/languages.yml /tmp/languages.json
brotli -f -q 11 -o extra_modules/language_db/data/languages.json.br /tmp/languages.json
```

Linguist resolves ambiguous extensions using content heuristics that xff deliberately does not run.
Generation retains xff's documented preferred winner where one exists. Other claims are preserved
as `shared_extensions` and `shared_filenames` for overlapping `-lang` and rg type filtering. For
example, `.h` matches C, C++, and Objective-C while `{lang}` remains C. Without a preferred winner,
the display label remains empty even when filtering candidates exist. Runtime JSON overlays can
replace preferred mappings or add explicit shared candidates. See `docs/design-rg.md` in the main
repository for precedence and overlay examples.
