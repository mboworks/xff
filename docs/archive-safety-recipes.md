# Archive tasks with explicit safety profiles

Use a full build with archive support. Reading an archive, creating its container, changing a
member, and writing an extracted file are separate operations. The profiles below keep safe mode
active and permit only the capabilities each example needs. They do not sandbox child programs;
pager commands remain a separate policy concern.

## Example user configuration

Add these settings to the user INI at `<OS account home>/.config/xff/config`, adapting them to
existing policy. Category selection belongs once in the unsectioned system or user INI, never in
an explicit `.xffrc` or on the command line. These profiles cannot remove an administrator's
unconditional blocks. Select one profile per invocation; composing profiles combines their
settings in normal application order.

```ini
--block-policy-categories=archive,output
--output-root=${HOME}/xff-output
--safe

[pack-new]
--no-safe-block-archive-writing
--no-safe-block-output-file-writing

[delete-member]
--no-safe-block-archive-writing
--no-safe-block-archive-overwrite
--no-safe-block-archive-content-deletion
--no-safe-block-output-file-writing
--no-safe-block-output-file-overwrite

[write-output]
--no-safe-block-output-file-writing
```

Create `$HOME/xff-output` before using these examples; xff does not create the declared root for
you. `--output-root` authorizes a scope, not a filename rewrite. The INI expands `${HOME}`, while
the shell expands `$HOME` in the commands. An authoritative system policy should use an appropriate
fixed absolute root: environment substitution does not establish trust in a caller-chosen path.
The user-config location itself comes from the OS account, independently of `HOME`.

| Task                         | Profile         | Capabilities permitted                                                                                   |
| ---------------------------- | --------------- | -------------------------------------------------------------------------------------------------------- |
| Inspect archive members      | none            | No mutation or execution capabilities                                                                    |
| Create a new archive         | `pack-new`      | archive-writing, output-file-writing                                                                     |
| Delete a member by rewriting | `delete-member` | archive-writing, archive-overwrite, archive-content-deletion, output-file-writing, output-file-overwrite |
| Create an ordinary report    | `write-output`  | output-file-writing                                                                                      |

The destination controls in this table apply because the example archive is inside the output
root. A rewrite outside declared roots additionally needs ordinary file-writing and file-overwrite.
With these profiles, those operations stay blocked. Overlapping temp/output roots require both
scopes to permit the operation. Directory creation and deletion remain blocked, so create any
needed output subdirectories before running xff.

## Inspect without mutation

```sh
xff "$HOME/xff-output/source.tar" --archive=roots -type f -print
```

Safe mode permits reading and listing. An attempted `-delete`, `--pack`, or `-exec` remains blocked
without a suitable profile. A dry run can preview supported actions, but it does not override
blocks. Inspection does not require `--archive-extract`; that flag materializes members for child
commands and therefore involves additional writing and execution capabilities.

## Create a new archive

```sh
xff source -type f --config=pack-new --pack="$HOME/xff-output/new.tar"
```

This packs ordinary source files under relative member names. New archive creation needs archive
writing; it does not need archive-content-writing or overwrite permission. The output scope also
requires output-file-writing. Repeating this command with an existing `new.tar` fails, preserving
that archive. An output path outside the declared scope fails too. An ordinary `-fprint` outside
the scope is still blocked.

This recipe uses one source root to avoid member-name collisions. Packing source members directly
from another archive is unsupported; extract or materialize an ordinary source tree first under a
separately reviewed policy. Reader support does not imply writer support: choose a supported pack
format, such as tar, rather than assuming every readable format can be produced.

## Delete one member, preserving its container and other members

```sh
xff "$HOME/xff-output/new.tar" --archive=roots --config=delete-member \
  --archive-delete -type f -name obsolete.txt -delete
```

Member deletion rewrites the existing container. It needs archive-writing, archive-overwrite,
archive-content-deletion, and the destination's write/overwrite permissions. It does not require
ordinary file-deletion. `--archive-delete` explicitly enables member rewriting; without it, member
deletion is rejected. An unconditional `--block-archive-content-deletion` still rejects the command,
even with this profile or `--skip-unsupported`.

Selecting the archive container itself is ordinary file deletion and remains blocked. Rewriting is
format-dependent; unsupported containers fail rather than becoming editable merely because the
profile permits deletion. Use a disposable tar fixture to validate a new workflow, then inspect
the surviving members and content.

## Write only beneath the output root

```sh
xff source -type f --config=write-output -fprint "$HOME/xff-output/files.txt"
```

The report may be created only as a new file under the output root or its existing subdirectories.
An existing report cannot be overwritten; a path outside the root is blocked. This profile does
not permit archive creation, member deletion, execution, or creating missing parent directories.
The root itself remains protected from replacement or deletion.

For policy composition and mandatory enforcement, see [Safety policy](design-safety.md).
For directory and archive destination interactions, see
[Directory-scoped controls](design-directory-safety.md). Inspect resolved settings with `--explain`
before evaluating an unfamiliar configuration.
