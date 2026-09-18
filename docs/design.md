# xff design

## Purpose and authoritative references

xff combines filesystem search, content matching, structured reporting, and explicit actions in
one find-style command. The generated [reference](../XFF.md) describes implemented vocabulary.
Focused design documents specify the contracts below. Proposed work belongs in [TODO](../TODO.md);
a proposed spelling is not an available option.

## Principles

- Preserve supported find behavior under the find flavor; document differences in
  [find flavors](design-find-flavors.md).
- Keep option spelling, parsing, configuration, and help consistent.
- Validate input before actions. Invalid configuration must not silently remove a restriction.
- Define capabilities and their self-documentation together in the registries.
- Route filesystem access through the VFS and enforce mutation policy at the host adapter.

## CLI grammar and flag types

The shape is `xff [whole-run options] [roots...] [expression]`. There are no subcommands.
The default root is `.`. Expression evaluation preserves order and short-circuiting; adjacent
predicates mean AND. Config expressions are grouped and ANDed with the CLI expression.

| Type                     | Example                  | Contract                                                 |
| ------------------------ | ------------------------ | -------------------------------------------------------- |
| Whole-run option         | `--sort=tree`            | Recognized at option boundaries throughout the command   |
| Expression test          | `-name '*.cc'`           | Evaluated for each visited entry                         |
| Expression action        | `-printf '%p\n'`         | Produces output or requests an operation                 |
| Expression operator      | `-o`, `!`, parentheses   | Combines expressions using the documented precedence     |
| Compatibility alias      | `-h`, `-q`               | A specifically supported alias with documented placement |
| Config-only directive    | `--require-user-globals` | Valid only in permitted config locations                 |
| Command-line-only option | `--xffrc=task.rc`        | Controls bootstrap/discovery; rejected inside INI files  |
| Sign-suffixed control    | `--rc`, `--rc-`, `--rc+` | Uses that family's documented modes                      |

Double-dash options are not hoisted out of a primary's argument run: inside an `-exec`
command they belong to the child. Bare `--` ends option recognition. Single-dash globals
retain their documented leading placement.
Unknown options remain usage errors. Close registered spellings may be suggested, but are never
accepted automatically. A recognized short global in an expression gets a placement hint; a
position-independent long form is named only if the registry provides one. Tokens inside primary
argument runs, or after `--`, are never rescanned to generate these hints.

Boolean pairs, enum values, repeatable lists, keyed options, and sign suffixes are distinct
mechanisms. Accepted forms and repetition behavior come from the flag's metadata or compatibility
parser; `--help=NAME` provides the specific contract.

## Configuration and safety

[Configuration](design-config.md) specifies the shared INI grammar, file order, selectors,
refinements, composition, controls, and complete examples. System config is `/etc/xff.ini`;
user config is the effective OS account home followed by `.config/xff/config`. Environment
variables cannot redirect these policy sources.

Missing automatic files are normal. Explicit files must exist. Existing unreadable files and
invalid globals are errors. Invalid named sections are disabled atomically, and selecting one is
an error. Explicit and composed names must exist in active files, except for plain built-in styles.

Xffrc autoloading defaults off. `--rc` loads root-directory files; `--rc+` includes descendants.
Discovery finishes before execution, does not follow directory symlinks or enter archives,
and applies to the whole invocation. Autoloaded globals require a trusted grant. Explicit
`--xffrc=FILE` is independent and non-arming; a file cannot authorize its own admission or actions.

[Safety](design-safety.md) defines the capability table and
[directory-scoped safety](design-directory-safety.md) defines path rules. Xff starts unrestricted.
Unconditional `--block-*` flags accumulate and cannot be cleared. `--safe` activates the configured
profile; `--no-safe` disables only that profile. The `--safe-block-*` pairs define membership.
`--dry-run` previews permitted actions without commands or mutations; it does not override blocks.

Writing, overwrite, deletion, execution, and directory operations have distinct controls.
Per-file `--block-policy-categories` determines whether archive/temp/output controls are separate.
Declarations are translated before policies combine, so later detail cannot erase an earlier block.

The model protects ordinary use, generated commands, tests, and declared filesystem roots.
It is not a sandbox around a deliberately hostile user or arbitrary child process. Pager commands
are separate from execution blocks. Resource exhaustion requires independent limits. Permission to
overwrite an archive can replace its whole contents; member-level controls do not prevent that.

## VFS and mutation enforcement

Consumers use capability-oriented `vfs::FileSystem` operations with value-returning status results.
Unsupported operations fail explicitly. POSIX handles and required raw pointers stay inside narrow,
annotated adapters. All file mutation routes through the common mutation surface.

Checks precede creation, truncation, publication, deletion, and directory changes. Exclusive creation
protects existing entries when overwrite is blocked. Directory policies pin roots, prevent traversal
through untrusted links, intersect overlapping restrictions, protect roots themselves, and preserve
hard-linked content through scoped replacement. Owned scratch cleanup is distinguished from deleting
caller-owned files. Archive staging/publication uses the same control surface.

The host-I/O checker guards this routing. Mutation and directory-policy tests exercise refusal,
collisions, links, overlapping roots, publication, and failure cleanup. These checks support the
stated threat model; they are not a proof against every adversarial filesystem race.

## Matching, traversal, and output

[Field validation](design-field-validation.md) defines static template errors, runtime-absent
values, shared printf parsing, and validation before actions.

RE2 is the default regex engine. Optional PCRE2 is selected explicitly and has built-in match/depth
limits; there is no silent fallback. Native ERE, literal, and glob modes have their documented
registry contracts. Patterns need not be portable between engines.

`-content` / `-icontent` perform literal content matching; `-rxc` / `-irxc` perform regex matching.
`-grep` outputs matching lines. Content matching skips binary files using the documented NUL
heuristic. There is no general input-transcoding option. Content reads, hashes, and negative content
tests can require full file reads.

`--hidden` / `--no-hidden` control dotfiles with flavor-specific defaults. `--case` and the
insensitive primaries control matching. Ignore files and include/exclude patterns are traversal
filters, distinct from config discovery. [Parallel traversal](design-parallel.md) defines ordering,
worker responsibilities, and bounded read-ahead; directory concurrency does not reorder actions.
[Execution resources](execution-resources.md) separates retained state, content reads, and concurrency,
and defines the measurements needed before optimizing them.

Streaming, aligned, structured, and aggregate output modes have explicit buffering controls.
Plain output preserves path bytes by default; `--path-encoding=escape` escapes controls.
`-print0` preserves exact path bytes with NUL separators. Structured formats escape their delimiters,
but arbitrary non-UTF-8 paths can still produce invalid Unicode text: no general transcoding guarantee
is made. [The help model](design-help-model.md) describes generated plain, Markdown, HTML, and roff help.

Exit status is `0` for successful execution and `2` for errors. With `--quiet` or `--exit-match`,
`1` means no matches. Errors take precedence over match status.

## Specialized features and development

Archives and compressed streams are composable extras; virtual entries retain their source identity
and host extraction/writing requires the corresponding capabilities. `--shards` supports the documented
`of`, `dotnum`, and `underscore` schemes, with custom `--shard-pattern` definitions and explicit display
and duplicate controls. [Physical files and logical shard sets](shard-populations.md) explains
which population predicates, actions, summaries, and comparison use. The reference supplies the
complete current vocabulary.

[Near-duplicate design](design-near-duplicates.md), [implementation planning](implementation-plan.md),
[test planning](test-plan.md), and [coverage policy](coverage.md) describe their boundaries and gates.
macOS and Linux are tested in CI. Other platforms, additional backends, and proposed options remain
roadmap work until implemented and self-documented.
