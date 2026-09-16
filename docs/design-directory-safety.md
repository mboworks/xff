# Directory-scoped safety controls

Directory controls extend [the safety model](design-safety.md) with explicit temporary and output roots.

## Scope and composition

Temporary and output directories share one permission model. A declared root covers its descendants
recursively, including subdirectories. It does not authorize removal or replacement of the root
itself. Merely placing an existing file in a temporary directory does not make that file owned
scratch storage.

`--block-policy-categories=archive,temp,output` extends the existing per-file interpretation rule.
Each INI independently selects which categories have dedicated controls. An ordinary file block in
a file that does not select a category expands into that category's block before configurations
are combined. Later category selection cannot undo the resulting unconditional blocks.

Execution remains a separate capability. No directory permission confines an executed child.
Mandatory filesystem restrictions therefore require blocking execution as well.

Root declarations must retain their trust provenance. A user setting must not move or enlarge a
system-defined scope; named groups and explicit files must not silently redirect it. The
boundary is unsectioned system/user configuration, with system declarations authoritative. A root
is explicit: reading `TMPDIR` for scratch placement does not itself grant permissions to everything
beneath that environment value.

## Control vocabulary

The category prefixes are `file`, `temp-file`, and `output-file`. Their operation suffixes are
`writing`, `overwrite`, and `deletion`. Directory operations use `directory-creation` and
`directory-deletion`, with corresponding `temp-` and `output-` prefixes. Every capability has an
accumulating `--block-` flag and the existing positive/negative `--safe-block-` profile pair.
Root declarations are `--temp-root=PATH` and `--output-root=PATH`. Each may occur once per
permitted file. The system declaration wins; otherwise the user declaration applies.
Roots must already exist and use absolute physical paths without symlink components.
For example, use `/private/tmp` rather than `/tmp` on macOS. A filesystem root (`/`) is invalid.

INI values may use `${TMPDIR:-/private/tmp}` or `${HOME:?HOME must be set}/xff-results`.
See [environment substitutions](design-config.md#environment-substitutions) for quoting and errors.
The expanded path passes the same validation and scope enforcement. Such a declaration explicitly
trusts the caller-controlled environment to choose that root. Defaults and required-value checks
are not trust checks; use literal paths for administrator-enforced fixed boundaries. Merely setting
an environment variable grants no directory exception without a matching root declaration.

| Operation                       | Ordinary destination                                        | Temporary-root descendant                                        | Output-root descendant                                             |
| ------------------------------- | ----------------------------------------------------------- | ---------------------------------------------------------------- | ------------------------------------------------------------------ |
| Create file                     | file-writing                                                | temp-file-writing                                                | output-file-writing                                                |
| Overwrite file                  | file-writing, file-overwrite                                | temp-file-writing, temp-file-overwrite                           | output-file-writing, output-file-overwrite                         |
| Delete file or symlink          | file-deletion                                               | temp-file-deletion                                               | output-file-deletion                                               |
| Create directory                | directory-creation                                          | temp-directory-creation                                          | output-directory-creation                                          |
| Delete empty directory          | directory-deletion                                          | temp-directory-deletion                                          | output-directory-deletion                                          |
| Delete nonempty directory       | Each descendant's deletion control, then directory-deletion | Each descendant's deletion control, then temp-directory-deletion | Each descendant's deletion control, then output-directory-deletion |
| Delete or replace declared root | Root is protected                                           | Root is protected                                                | Root is protected                                                  |

The table assumes the destination category is selected in the originating INI. Otherwise its
ordinary controls expand to the dedicated controls. If a path belongs to both temp and output
scopes, both sets must permit the operation. When archive detail is selected, physical archive
writes must additionally satisfy archive controls; path permissions cannot unblock archive blocks.
With any declared roots, archive output outside all roots also requires ordinary file writing/overwrite
permission. Select `archive` alongside the directory categories to permit archives within those roots
while blocking ordinary writes elsewhere; leaving `archive` unselected expands an ordinary file block
into an unconditional archive block that directory grants cannot remove.

A temporary root also supplies placement for extraction and mount scratch. An output root defines
where its permissions apply; it does not silently rewrite explicit output filenames. Archive
publication staging must remain on the destination filesystem so atomic publication still works.

## Operations

File creation, file overwrite, file deletion, directory creation, and directory deletion are
separate decisions. Overwriting requires writing permission as well as overwrite permission.
Deleting a directory requires directory-deletion permission; recursively removing a tree also
requires permission for each affected descendant. A directory permission cannot stand in for
permission to delete its files.

Owned scratch cleanup remains attached to the handle that created that scratch storage. It does
not permit deleting arbitrary pre-existing entries under the same root. Archive staging is an
implementation detail of an authorized archive write; final publication must independently check
the destination's policy. A temporary pathname must never authorize publication elsewhere.

Archive content controls continue to govern member edits. Directory controls concern the physical
archive destination, not synthetic member pathnames. Overlapping directory scopes must combine
restrictions rather than choosing whichever scope permits more.

## Enforcement

Path classification and mutation must share the same filesystem authority. String-prefix checks
and canonicalize-then-open checks are insufficient: a symlink or rename can change what the later
operation touches. Scoped operations use retained directory handles and descriptor-relative
mutation adapters. Roots are pinned for the run; replacing a root pathname does not redirect
its retained handle. Parent traversal (`..`) is rejected while directory policies are present. Scoped exceptions must not follow symlinks outside their authorized root.
Deleting a symlink affects its own directory entry, not its target.

Hard links need explicit treatment too: in-place overwrite of a multiply-linked file can modify
content accessible outside the root. Scoped overwrite creates a fresh inode and replaces the directory entry, preserving the
content of other hard links. It does not preserve the previous inode or its metadata. Exclusive creation must keep its atomic collision guarantee.

Policy checks belong in the common mutation layer for ordinary output, archive publication,
extraction, mount-directory lifecycle, and recursive cleanup. Early expression validation must
allow operations that are permitted for some paths, with the per-path decision enforced when the
target becomes known. Dry-run validates those same decisions and opens no mutating handles.

## Verification

- Complete system/user INIs with different category selections, named profiles, and attempted
  later weakening of mandatory blocks.
- Root itself, children, grandchildren, similarly prefixed siblings, `..`, missing parents, and
  overlapping roots.
- Parent/leaf symlinks, dangling symlinks, hard links, and replacement at the mutation boundary.
- File creation/overwrite/deletion, directory creation/deletion, and recursive mixed-tree removal.
- Archive creation/replacement and member edits inside and outside directory scopes.
- Extraction and mount scratch placement, owned cleanup, and pre-existing temporary files.
- Dry-run and collision failures that preserve existing content.

## Example

Both directories must already exist. This system INI permits new result files and scratch writes,
while blocking ordinary writes, execution, and replacement or deletion of existing results.

```ini
--require-system-globals
--block-policy-categories=archive,temp,output
--output-root=/srv/xff/results
--temp-root=/srv/xff/scratch
--block-execution
--block-file-writing
--block-file-deletion
--block-directory-creation
--block-directory-deletion
--block-output-file-overwrite
--block-output-file-deletion
--block-output-directory-deletion
```

`xff . -fprint /srv/xff/results/list` can create a new result; running it again fails on the
existing destination. `--no-safe` cannot remove any of these unconditional blocks.
Selecting separate categories without declaring their roots in the system INI delegates those root
declarations to the user INI; declare the roots globally when their scope must be authoritative.

Ordinary temporary extraction and mount preparation require both writing and directory-creation
permission. Archive-owned staging is part of an authorized archive write and does not require
separate directory-creation permission. Owned cleanup remains allowed; it cannot select existing
directories.
Archive staging stays beside its destination for atomic publication. File output permissions do not
redirect output names, and temporary permissions do not authorize publishing a file elsewhere.

These controls do not promise rollback, preserve inode metadata on scoped overwrite, or sandbox
child processes. Concurrent filesystem changes can cause errors or partial completion; retained
directory handles prevent a symlink substitution from redirecting an authorized operation.
