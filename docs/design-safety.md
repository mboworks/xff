# Safety policy and previews

xff starts unrestricted when no configuration requests restrictions. Safety has three independent
parts: unconditional capability blocks, a configurable safe-mode profile, and dry-run previews.
Explicit-file arming (`--allow-exec`) covers execution and deletion only. An unarmed explicit file
can request file/archive output, including overwrite, when the safety policy permits it.
These controls govern xff's actions. They are not a sandbox around child programs, an output-directory
confinement mechanism, or a guarantee against resource exhaustion.

## Capabilities

File controls also cover archive operations under the default empty category list. With `archive` selected,
archive operations use `archive-writing`, `archive-overwrite`, and the three
`archive-content-writing`, `archive-content-overwrite`, `archive-content-deletion` controls instead.
Each has an unconditional `--block-` flag and a positive/negative `--safe-block-` profile pair.
The operation table below specifies every required control.

| Capability | Unconditional flag       | Profile pair                                                     | Operations covered                                                                      |
| ---------- | ------------------------ | ---------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Deletion   | `--block-file-deletion`  | `--safe-block-file-deletion` / `--no-safe-block-file-deletion`   | Filesystem entry removal and archive-member deletion                                    |
| Execution  | `--block-execution`      | `--safe-block-execution` / `--no-safe-block-execution`           | `-exec`, `-execdir`, `-ok`, `-okdir`, `-capture`, and `-capturedir`                     |
| Writing    | `--block-file-writing`   | `--safe-block-file-writing` / `--no-safe-block-file-writing`     | Named file output, archive creation/modification, and temporary extraction for commands |
| Overwrite  | `--block-file-overwrite` | `--safe-block-file-overwrite` / `--no-safe-block-file-overwrite` | Truncating, modifying, or replacing existing content, including archive rewrites        |

Writing includes overwriting. Allowing writing while blocking overwrite permits only new output.
Deletion is separate: unlinking an entry is not classified as a content write. Removing an archive
member also rewrites existing content, so it requires deletion, writing, and overwrite permission.
Normal results and diagnostics on stdout/stderr remain available. Shell redirection happens outside
xff and is outside these controls.

## Composition and activation

All profile blocks initially apply, but safe mode initially is off. `--safe` enables the
current profile; `--no-safe` disables it. Profile settings and activation follow normal configuration
application order, with the last applicable setting winning. Activating the profile does not reset
its settings. Unselected named sections have no effect.

Unconditional blocks only accumulate. There are no `--no-block-*` options. Neither another group,
a later file, the CLI, nor `--no-safe` can remove an unconditional block that has been applied.
For each capability:

```text
blocked = unconditional block OR (safe mode active AND profile block)
```

These flags may be supplied globally or in named configuration sections and on the CLI. A mandatory
administrator policy belongs in unconditional system globals. `--require-system-config` prevents
skipping that file; `--require-user-config` can similarly ensure user policy is applied. Permission
to skip a policy file intentionally permits omitting its policy. Mutable profiles are not mandatory
prohibitions.

A system policy that always forbids execution, but offers other operations in an unsafe profile:

```ini
--require-system-config
--block-execution
--safe

[unsafe]
--no-safe
```

`xff . --config=unsafe` still cannot execute a child program. The same holds for CLI `--no-safe`.

An administrator can instead provide an optional profile that only blocks execution and deletion:

```ini
--no-safe
--safe-block-execution
--safe-block-file-deletion
--no-safe-block-file-writing
--no-safe-block-file-overwrite

[safe]
--safe
```

Both CLI `--safe` and `--config=safe` activate this configured profile. Without this configuration,
`--safe --no-safe-block-file-writing` allows new files/archives but still blocks overwrite, deletion,
and execution. Add `--no-safe-block-file-overwrite` to permit replacing content as well, provided no
unconditional block prohibits it.

Allowing arbitrary execution allows the child to delete, write, or overwrite data regardless of
xff's other capability settings. Use `--block-execution` for a mandatory boundary. Prompting with
`-ok` does not exempt execution from policy.

## Enforcement and collisions

Statically prohibited actions are rejected before traversal, including actions in branches that
might never match. Diagnostics name the prohibited capability and action. Config authorization
cannot bypass a capability block.

When overwrite is blocked, output-file creation must be atomic and exclusive. Existing regular
files, directories, and symlinks (including dangling symlinks) are collisions. A preliminary
existence check cannot authorize a later overwriting open. A file created by this run may receive
subsequent records through the same retained output handle.

Archive generation uses a unique, exclusively created temporary output and publishes without
replacing an existing destination when overwrite is blocked. Failed publication preserves the
existing destination. Parent-directory symlinks remain supported; this is content protection,
not confinement to a designated directory tree.

Safe mode does not promise rollback: new files or earlier permitted actions can remain after a
later error. Ordinary creation can consume disk space. Internal temporary-file cleanup remains
necessary even when user-directed deletion is blocked.

## Dry run

`--dry-run` previews permitted actions instead of performing them. Resolve policy first: dry run
never grants permission to a blocked operation. Traversal, reads, normal result output, and
diagnostics still occur; output destinations are not created, truncated, or replaced.

| Operation                             | Preview                                                                          |
| ------------------------------------- | -------------------------------------------------------------------------------- |
| File/directory deletion               | Report the selected path without removing it                                     |
| Archive-member deletion               | Report members without rewriting the archive                                     |
| Named file output                     | Report the destination and whether output would create or replace content        |
| Archive generation                    | Report the destination and selected entry count without writing an archive       |
| Commands, including prompted commands | Report arguments and working-directory context; launch nothing and do not prompt |
| Capture commands                      | Report the command; do not manufacture captured values                           |

A skipped command has no exit status, and a skipped capture has no value. If a preview reaches such
an action, report that the remaining expression for that entry cannot be evaluated. Stop evaluating
that entry rather than inventing a result, including through negation, OR, and comma operators.
Such an incomplete preview returns an error status. Batch previews describe selected invocations;
they do not claim to predict operating-system argument-limit chunking.

Dry-run checks can report an existing collision, but cannot guarantee the filesystem will remain
unchanged between preview and a subsequent real run. The real operation enforces permissions and
exclusive creation again.

## Flag boundaries

The safety vocabulary is `--block-*`, `--safe`, `--no-safe`, the `--safe-block-*` pairs, and
`--dry-run`. No separate activation requirement, default-safe setting, force switch, or unsafe
alias is necessary. Archive capability flags still select archive features; they do not override
policy. Config-file requirement directives govern whether policy files may be skipped.

## Archive blocking policy

`--detailed-block-policy=LIST` is config-only, once before sections in each system or user file. Each file chooses its own policy. Named sections, explicit `.xffrc` files and the CLI cannot set it. The default is an empty category list. The comma-separated list currently accepts `archive`; unknown categories are errors.

Every control listed must permit the operation. Names omit `--block-` and the active `--safe-block-` prefixes.

| Operation                      | archive not selected (default)              | archive selected                                                                       |
| ------------------------------ | ------------------------------------------- | -------------------------------------------------------------------------------------- |
| What switches                  | Archives and members use file controls      | Archive output and member edits use archive controls                                   |
| Create ordinary file           | file-writing                                | file-writing                                                                           |
| Overwrite ordinary file        | file-writing, file-overwrite                | file-writing, file-overwrite                                                           |
| Delete file or entire archive  | file-deletion                               | file-deletion                                                                          |
| Pack new archive and members   | file-writing                                | archive-writing                                                                        |
| Pack replacement archive       | file-writing, file-overwrite                | archive-writing, archive-overwrite                                                     |
| Add member to existing archive | file-writing, file-overwrite                | archive-writing, archive-overwrite, archive-content-writing                            |
| Replace existing member        | file-writing, file-overwrite                | archive-writing, archive-overwrite, archive-content-writing, archive-content-overwrite |
| Delete existing member         | file-writing, file-overwrite, file-deletion | archive-writing, archive-overwrite, archive-content-deletion                           |
| Extract to new ordinary file   | file-writing                                | file-writing                                                                           |
| Extract over existing file     | file-writing, file-overwrite                | file-writing, file-overwrite                                                           |
