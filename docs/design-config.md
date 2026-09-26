# xff configuration system design

Status: design of record for the shipped configuration system. This document
refines the configuration and safety decisions in [`design.md`](design.md).

[Inspecting configuration](config-inspection.md) explains the `--explain` source, profile,
application, and safety views with small complete examples.

## Goals

Configuration should make repeatable command lines convenient without allowing a
file found in a searched repository to execute commands. Resolution must be
inspectable, command-line choices must win, and an explicitly loaded file must
not be able to grant itself permission to run a dangerous directive.

## Sources and precedence

xff applies trusted defaults, opt-in project files, and the command line:

| Precedence | Source              | Grammar | Trust and purpose                                                  |
| ---------- | ------------------- | ------- | ------------------------------------------------------------------ |
| lowest     | `/etc/xff.ini`      | INI     | root-owned defaults and authoritative controls                     |
|            | user config         | INI     | trusted personal defaults and named configurations                 |
|            | autoloaded `.xffrc` | INI     | opt-in, non-arming project files before CLI processing             |
|            | `--xffrc=FILE`      | INI     | explicitly loaded, repeatable, non-arming files                    |
| highest    | command line        | CLI     | explicit flags and selectors; later conflicting values usually win |

Autoloading is off by default (`--rc-`). `--rc` loads `.xffrc` in directory search roots;
`--rc+` includes descendants. No ancestor search occurs. Per-directory `.gitignore`, `.ignore`,
and `.xffignore` files remain traversal inputs rather than configuration sources.

The user config is always `<OS account home>/.config/xff/config`. The effective OS
account record supplies the home directory; environment variables cannot redirect this policy
file. There is no system directive for relocating it.

Missing automatic system or user config files are normal. An explicit `--xffrc=FILE`
must exist. Automatic and explicit config paths must resolve to regular files; directory and FIFO inputs are errors.
Symlinks to regular files are permitted for trusted/explicit paths, while autoloaded entries must
themselves be regular files to avoid discovering configurations through links. Every existing config must be readable; permission failures and dangling symlinks
are errors, including when a skip flag is requested. `--explain` reports consulted sources.

The position-independent `--no-system-config` and `--no-user-config` exclude the named
sections in their respective files. `--no-config` excludes both sets and disables `.xffrc`
autoloading regardless of flag order. Explicit `--xffrc=FILE` inputs and ignore files remain active.

**Existing unsectioned globals remain active by default.** A corresponding
`--no-require-system-globals` or `--no-require-user-globals` permits excluding those globals too
when a skip is requested. The permission alone does not skip anything. Missing automatic files
remain normal; neither requirement makes a missing file mandatory. Every existing file is still
read and validated, including when all of its ordinary settings are skipped.

| Policy                           | No skip requested | Corresponding skip requested               |
| -------------------------------- | ----------------- | ------------------------------------------ |
| Omitted or `--require-*-globals` | Globals apply     | Globals apply; named sections are excluded |
| `--no-require-*-globals`         | Globals apply     | Globals and named sections are excluded    |

- System pair: `--require-system-globals` / `--no-require-system-globals`, unsectioned system config only.
- User pair: `--require-user-globals` / `--no-require-user-globals`, unsectioned system or user config; the system decision wins, even if its own ordinary globals are skipped.
- Each pair may appear once per permitted file.
- Neither pair is allowed in named sections, explicit `.xffrc` files, or on the CLI.
- `--no-config` applies both decisions independently. Required globals are retained; the skip itself is not an error.
- A named section excluded from one tier may still be selected from another tier. A name absent
  from every active file is an error, including a composition reference from a retained global.

For example, this system file keeps its deletion block active under `--no-system-config` or
`--no-config`, while the named section is excluded:

```ini
--require-system-globals
--block-file-deletion

[listing]
--color=never
```

A duplicate require directive is a hard error, even if both occurrences have the same value.

`--allow-xffrc` / `--no-allow-xffrc` control admission of both explicit files and automatic discovery. They may occur in system
unsectioned globals or user blocks. A system global denial cannot be overridden by the user file,
a named section, an explicit file, or suppression of system defaults. User decisions follow the
same application order as ordinary settings, including in-place `--config=NAME` composition.
Section declaration order does not override explicit selector order. Each section applies once.
For example, with this user INI:

```ini
[deny]
--no-allow-xffrc
[allow]
--allow-xffrc
```

`xff . --config=allow --config=deny --xffrc=task.rc` rejects loading before opening `task.rc`;
reversing the two selectors permits loading. The same admission decision governs `--rc` and
`--rc+`. Admission is resolved from trusted configuration before loading any `.xffrc`, so a file
cannot select a section to authorize its own loading.

Explicit `.xffrc` files cannot contain the require/no-require pairs or
`--allow-xffrc` / `--no-allow-xffrc`; these controls govern automatic files or permission
to load an explicit file, so that file cannot grant itself permission. `--block-policy-categories`
is also restricted to automatic config files. In contrast, `--allow-exec` is a
separate runtime flag accepted on the CLI and in config files, but an explicit file's
own `--allow-exec` never arms its dangerous directives.

A skip request never discards required globals. An unavailable selected name or an invalid existing
file remains an error.

## Shared file grammar

System, user, and all `.xffrc` files accept the same INI grammar: unconditional
options before the first section, then plain named configuration sections. Every section name, including `[global]`, `[defaults]`, and `[policy]`, is an ordinary
named configuration:

```ini
--no-require-system-globals
--no-require-user-globals
--color=auto

[dev]
--color=always
-E

[prod]
--color=never

[policy]
--hidden
```

The option spelling is exactly the command-line spelling: write `--color`, not `color`, and `-E`,
not a derived configuration key. Use `--color=auto`: whitespace separates arguments, so
`--color = auto` is not an alternate assignment syntax. The resulting arguments are validated
by the same flag parser as command-line arguments.

One line may contain multiple directives: `--hidden --color=never` or `-name foo -name bar`.
Each primary consumes only its own arguments. Adjacent predicates mean AND, so the latter matches
nothing; use `-name foo -o -name bar` for either name. Parentheses group expressions. Config
predicates and actions are parsed as an expression, then ANDed as a group with the command-line
expression; global options retain their application order. `-type` and `-xtype` accept only the
registered letters `b,c,d,f,l,p,s`, individually or as non-empty comma lists. `-type garbage` is
a usage error on the CLI and in config globals; in a named section it disables that section.

Every unconditional line is validated. An invalid line is a hard error with its file,
line number, source text, and command-line validation error. Execution never continues with only
part of the globals applied: a malformed option must not discard a restriction on the same line.
This also applies when skipping a trusted file, which must still be inspected for mandatory policy. A named section is atomic: one invalid line disables the complete section,
so a partially applied configuration is impossible. A section that selects a disabled section with
`--config=NAME` is disabled transitively. Loading continues so independent sections remain usable,
but explicitly selecting any directly or transitively disabled section is a usage error. For
example, `-E development` is invalid because `-E` takes no value; `development` is an unexpected
token, and the containing section is disabled.

There is no special policy rule language. System-only controls belong before the first section;
placing one in a named section disables that section atomically. `--block-policy-categories` selects how this file interprets its safety blocks, including in named sections.
Its value never changes the interpretation of another file. See [Safety](design-safety.md).

Bootstrap flags `--no-config`, `--no-system-config`, `--no-user-config`, `--xffrc`,
`--explain`, and help-only `--width` are command-line only. Using one in config globals
fails; using one in a named section disables that section. Metadata requests such as
`--help`, `--man`, and `--version` likewise belong on the CLI.
Listing `--pager` / `--no-pager` settings follow normal configuration order. Metadata output
is resolved before config loading, so its pager comes from CLI/environment settings.
Pager command restrictions are separate from safety blocks and remain a deferred design topic.

### Names, refinements, and composition

An explicit or composed `--config=NAME` must match a declaration in an active, admitted
file. Validation happens after all files are available, allowing forward and cross-file references.
Only the plain built-in styles `find`, `xff`, and `rg` need no declaration. An implicit
invocation-name selector may have no matching section. A name such as `xff:2` still needs a
declaration; its prefix selects a style, not a bundled version snapshot.

Declare each `[NAME]` at most once in a file, including empty sections. Repeating a name disables
all its declarations in that file; selecting it directly or through composition is a usage error.
A different file may declare the same name to refine it. Values apply in resolution order, so
later overrides win. Composition expands referenced sections in place, while refinements from
later files wait for the earlier file's section to finish. For example:

```ini
# /etc/xff.ini
[dev]
--color=auto
```

```ini
# user config
[dev]
--color=always
--config=checks

[checks]
-type f
```

```ini
# task.xffrc
[dev]
--color=never
```

`xff . --config=dev --xffrc=task.xffrc` applies all three `[dev]` contributions, selects
`[checks]`, and ends with `--color=never`. References such as `--config=checks` may repeat;
they compose configurations rather than declaring them. Each contributing line is applied at
most once, so repeated references and cycles do not repeatedly apply a configuration.

Names are literal, including colons: `[xff:debug]` requires `--config=xff:debug`; it does not
mean that both `xff` and `debug` must be active. `[common]` is an ordinary named config.
Unconditional flags belong before the first section.

The config reader emulates the shell's quoting and escaping to produce arguments for the same
CLI parser. It removes comments before passing those arguments to the parser. System, user, and
explicit `.xffrc` files all use this rule. Single quotes preserve
literal content; double quotes group content and allow backslash escapes for `"`, `\`, `$`, and
backtick. Outside quotes, a backslash escapes the next character. Adjacent quoted and unquoted
pieces form one argument; empty quotes produce an empty argument. Braced environment substitutions described below are supported outside single quotes. There is no
command, pathname, or tilde expansion, and config text is never executed as a shell script.

An unquoted `#` at the beginning of a word starts a comment through the end of that physical
line. Thus `foo#bar`, `\#`, and `"#"` are literal arguments. An unquoted, unescaped `;` starts
a comment anywhere outside quotes. Either marker may follow any amount of whitespace. Blank
lines are ignored. Quote or escape a literal semicolon: `\;` or `";"` supplies the `-exec`
terminator, just as on the command line. Comment text never reaches the CLI argument parser.

```ini
--color=never # Applies without a selector

[hash] # Activated with --config=hash
-name '#*' -o -name space\ name

[command]
-exec printf '%s\n' '#literal' \; ; Exec terminator before the comment
```

Quotes may span physical lines. A backslash followed by a newline continues the logical line
without adding a character, except inside single quotes. Diagnostics identify the logical
line's starting line number. An unmatched quote or a trailing backslash is a syntax error;
no partial arguments from that line are applied. Invalid global lines fail the invocation with a
diagnostic, while an invalid named-section line disables its entire section.

User and explicit config files may not redefine the exact built-in preset names `[find]`, `[xff]`,
or `[rg]`. Those lines are dropped with a warning. Use a custom named configuration and compose
the desired preset explicitly with `--config=find`, `--config=xff`, or `--config=rg`.

## Environment substitutions

System INI, user INI, and explicit or autoloaded `.xffrc` files share these forms:

| Form               | Meaning                                                    |
| ------------------ | ---------------------------------------------------------- |
| `${NAME}`          | Substitute the value; unset is an error, empty is allowed. |
| `${NAME:-DEFAULT}` | Use literal DEFAULT when NAME is unset or empty.           |
| `${NAME:?MESSAGE}` | Fail when NAME is unset or empty, with NAME and MESSAGE.   |

Names use ASCII letters, digits, and underscores, starting with a letter or underscore.
Substitution happens while reading the file, before flag validation and named-section selection.
An error in globals fails the invocation; an error in a named section disables that whole section,
including when it is not selected. Selecting that section then fails, as for other invalid lines.
Diagnostics retain the source file and starting line number. Values use xff's shared, read-once
process-environment cache.

```ini
--temp-root="${TMPDIR:-/private/tmp}"
--output-root="${HOME:?HOME must be set}/xff-results"

[report]
-name "${REPORT_PATTERN:-*.cc}"
-printf '%{env.HOME} %p\n'
```

The root directories must already exist and pass the normal root validation, including no symlink
components. These declarations remain restricted to unsectioned system/user INI. This example uses
`/private/tmp` for macOS; choose an appropriate physical directory on other systems.

Single quotes or an escaped dollar preserve literal text: `'${HOME}'`, `\${HOME}`, and
`"\${HOME}"` do not expand. Bare `$HOME` remains literal too. Double quotes permit substitution.
Adjacent literal and substituted pieces form one argument; even an empty substitution preserves
its argument. Values are never split into words or parsed again: whitespace, quotes, `#`, `;`,
backslashes, and further `${...}` inside a value remain content. There is no command substitution.

DEFAULT and MESSAGE are literal text ending at the next `}`; they may contain spaces but not
newlines or nested `${...}`. Quotes and backslashes inside them are literal, not a second quoting
language. Other operators, assignment, and recursive substitution are unsupported. A missing
closing brace is an error.

`%{...}` is not an INI substitution. It passes unchanged to the consuming flag. For example,
`-printf '%{env.HOME}'` uses the existing field renderer; `--output-root=%{env.HOME}` does not.
The CLI itself does not interpret `${...}`: any command-line substitution is performed by the
calling shell before xff receives its arguments.

Environment variables are caller-controlled. Using `${HOME}` or `${TMPDIR}` in a safety root
explicitly delegates that path choice to the caller; requiring a nonempty value or providing a
fallback does not make it trusted. Use literal roots and literal mandatory safety flags when the
administrator needs fixed boundaries. Substitution cannot change config discovery locations or
bypass the normal source restrictions and validation on directives.

## Selectors and invocation names

`--config=NAME` is repeatable. Every selector remains active for named-block
matching. Among selectors whose prefix is `find`, `xff`, or `rg`, the last one
chooses the built-in style baseline; custom names do not change the style.

The executable basename supplies the first selector:

- `find` selects the find expression vocabulary and find defaults;
- `xff` selects the xff style;
- `rg` selects the rg style;
- another basename such as `mytool` activates a same-named config block while
  retaining the xff style;
- `_full` is removed first, so `xff_full`, `find_full`, and `rg_full` behave as
  their base names.

Explicit `--config` selectors follow the invocation selector and can therefore
choose a later built-in style. Styles set baseline traversal and presentation
defaults; the find style restricts expression primaries, operators, and values,
but explicitly supplied xff global controls remain available.

Boolean capabilities use their own flag families or an existing option's value set.
Proposed spelling gates remain roadmap work in [TODO.md](../TODO.md).

## Safety and arming

Registry descriptors classify expression directives as safe, sensitive, or
destructive. A config line receives the most restrictive class of any directive
on that line; that aggregate class drives arming and diagnostics. Unconditional `--block-*` restrictions apply to actions from every source, including the CLI.
Use `--require-system-globals` to prevent omission of a mandatory system policy.

System and user configuration are trusted tiers. An explicit `--xffrc` file is a
non-arming tier: naming it authorizes loading it, not executing dangerous content
from it. Sensitive or destructive directives in that tier are dropped unless
`--allow-exec` is active from one of these sources:

- the command line;
- `/etc/xff.ini` defaults;
- an applying user-config line.

An explicit `--xffrc` file is deliberately excluded from that check, so it
cannot authorize itself. Unconditional safety blocks take precedence over arming. `--allow-exec` only controls config provenance; runtime protections such
as `--safe`, `--dry-run`, and action-specific confirmation remain independent.

## Tiny configuration examples

### Defaults and a named override

`/etc/xff.ini`:

```ini
--color=auto
[dev]
--color=never
```

`xff . --config=dev` applies `auto`, then `never`. `xff . --config=dev --color=always`
ends with `always`. `[dev]` is optional configuration; the unsectioned line applies on every run.
`[global]` would simply define another optional named configuration.

### Which globals survive a skip

`/etc/xff.ini`:

```ini
--require-system-globals
--no-require-user-globals
--block-execution
```

`xff . --no-config` retains the system execution block, excludes user globals and both sets
of named sections, and disables `.xffrc` autoloading. `--no-system-config` also retains the system
block while leaving user configuration active. A user `--require-user-globals` cannot override the
system grant. If the system omits its user pair, the user file decides for itself:

```ini
--no-require-user-globals
--color=never
```

The color applies normally and is omitted with `--no-user-config` or `--no-config`.
To permit excluding globals from both files, put these in the system file:

```ini
--no-require-system-globals
--no-require-user-globals
```

All requirement controls precede sections. Each pair may occur only once per permitted file.

### Require restrictions from system configuration

Keep `/etc/xff.ini` administrator-owned and unwritable by users whose configs it constrains:

```ini
--require-system-globals
--block-execution
--block-file-deletion
```

These restrictions apply to every source, including CLI expressions. Neither `--allow-exec`
nor `--no-safe` clears them. Add `--no-allow-xffrc` to reject explicit files before opening them.
This is an application policy, not an operating-system sandbox: someone able to replace the
executable or its mandatory configuration can replace the policy too.

### Choose archive controls independently in each file

`--block-policy-categories=LIST` may occur once before sections in a system or user INI.
It applies to that file's globals and named sections. Each file is translated independently;
a user selecting `archive` cannot remove restrictions contributed by a system file using the default empty list.
Select `archive`, `temp`, and/or `output` for dedicated controls. The default empty category list
applies ordinary file and directory blocks to every corresponding category. `--temp-root=PATH`
and `--output-root=PATH` are each permitted once in unsectioned system/user INI; system declarations
win. Named sections, explicit files, and CLI flags cannot declare these roots.
See `--help=safety` for the full operation table, including the distinction between packing a
replacement archive and editing its members.

### Deliberately arm one explicit file

With no system prohibition, `task.rc` may contain:

```text
-exec echo {} \;
```

`xff . --xffrc=task.rc` leaves that dangerous line inert. Supplying `--allow-exec` from the
command line or an applying automatic config permits it through the config gate. Putting
`--allow-exec` in `task.rc` itself never grants that permission. Arming does not bypass the
runtime guards on destructive actions.

## Explicit `.xffrc` files

An `.xffrc` file uses exactly the same INI format as the system and user files. Loading
it applies its unsectioned flags; each `[NAME]` section applies only when that name is
selected. For example, `task.xffrc` can contain both:

```ini
--hidden

[quiet]
--color=never
```

`xff . --xffrc=task.xffrc` applies `--hidden`. Adding `--config=quiet` also applies
`--color=never`. A name can also be selected by the invocation name or by another
config's `--config=NAME` directive; it need not be repeated on the command line.
Selecting a name does not discover files: `--config=quiet` alone does not load `task.xffrc`.
Unsectioned flags can themselves include `--config=NAME` to select a section when the
file loads. Config-only authority controls retain their source restrictions.

Each explicit file requires `--xffrc=FILE` and passes the admission and safety checks above.
Explicit loading is independent of the autoload mode and the globals permission described below.

## Autoloaded `.xffrc` files

| Mode    | Discovery                                              |
| ------- | ------------------------------------------------------ |
| `--rc-` | Off (default); explicit `--xffrc=FILE` remains active. |
| `--rc`  | Only `.xffrc` directly in each directory search root.  |
| `--rc+` | Root files and `.xffrc` in descendant directories.     |

Modes may appear on the CLI or in applying system/user globals and named sections; the last
setting wins. Discovery uses these trusted inputs before loading any `.xffrc`. An `.xffrc`
cannot set discovery flags, nor trigger another discovery pass by selecting a trusted section.
`--no-config` disables autoloading regardless of mode order. With no root argument, the root
is `.`. File and symlink roots do not load a parent's configuration. There is no ancestor
search, directory-symlink following, or discovery inside archives.

Discovery completes before actions execute. It visits physical directories independently of
ignore files, hidden-file settings, search predicates, depth limits, and pruning. Thus `--rc+`
can inspect a large tree even when the search itself is narrow. This also applies to `--explain`.
Missing `.xffrc` files are normal; unreadable files or directories and non-regular `.xffrc`
files (including symlinks) abort loading. No actions run with a partially discovered configuration.

Files affect the **whole invocation**, including other roots. Root arguments are processed in
order; each tree uses parent-before-child depth-first order with lexically sorted siblings.
Repeated or overlapping directories are discovered once. Discovered files follow trusted defaults
and precede CLI processing. Explicit files keep their CLI positions; explicitly naming an
autoloaded file applies it again at that position. Relative flag values are relative to the
working directory, just as in other INI files.

### Permission for unsectioned content

By default, autoloaded files may contain only named sections. They use the same INI grammar as
all other configs. Named sections apply when selected by `--config=NAME`, the invocation name,
or composition from another applying section; loading alone does not select every section.

```ini
# project/.xffrc
[checks]
-type f
```

`xff project --rc --config=checks` applies this file-only filter. Without the selector,
`[checks]` remains inactive.

The config-only pair `--allow-rc-globals` / `--no-allow-rc-globals` controls whether discovered
files may contain unsectioned content:

- Each permitted system or user INI file may contain one member of the pair once, before sections.
- A system denial is authoritative, including when system defaults are skipped.
- Otherwise an applying user choice overrides the system setting; without any grant, globals are forbidden.
- Named sections, explicit or autoloaded `.xffrc` files, and the CLI cannot set this permission.
- Explicit `--xffrc=FILE` keeps its unsectioned-content behavior and does not need this grant.

For example, a user INI can enable root discovery and permit globals:

```ini
--rc
--allow-rc-globals
```

The discovered project file can then contain both unconditional options and a selected profile:

```ini
# project/.xffrc
--hidden
[quiet]
--color=never
```

`xff project` applies `--hidden`; adding `--config=quiet` also applies `--color=never`.
Without the grant, this file **fails loading before actions run**. Rejection covers all unsectioned
directives, including expression primaries and `--config=NAME`, not just double-dash options.
Blank lines and comments are allowed. Forbidden content is never silently ignored: dropping an
intended filter or restriction could change the operation's meaning. The error identifies the
file and the missing permission.

Admission (`--allow-xffrc`), permission for globals (`--allow-rc-globals`), and action arming
(`--allow-exec`) are separate decisions. Admission is checked before probing roots. Neither
autoloading nor the globals grant arms dangerous directives. The non-arming gate, active safe
profile, and unconditional `--block-*` controls apply to both explicit and autoloaded files.
Trusted `--allow-exec` still permits dangerous directives through that gate, so combine it with
mandatory blocks when execution or deletion must remain prohibited. Even ordinary options can
change results across all roots: grant globals only when those configuration files are trusted.

## Resolution and inspection

After discovery, skip authorization, and policy gating, surviving flags are
applied in this order:

1. system defaults;
2. unconditional user-config lines and lines selected by the invocation name;
3. autoloaded files in discovery order, including their currently selected sections;
4. original command-line globals, in their original order;
5. immediately after each `--config=NAME`, system named sections, user and already-loaded `.xffrc`
   config lines newly activated by that selector;
6. immediately after each `--xffrc=FILE`, currently applicable lines from that
   file. Later selectors may activate its remaining lines.

Each config line is applied at most once. Consequently, selector placement is
observable when options conflict: in `--color=always --config=plain`, a
`[plain]` section containing `--color=never` wins, while reversing those two CLI
arguments makes `--color=always` win. Multiple explicit files retain their own
positions rather than collapsing into a single tier.

The ordinary option resolvers then apply their documented conflict behavior,
usually last value wins. This preserves provenance, command-line order, and one
set of option semantics.

`--explain` performs enabled rc discovery but does not evaluate the search expression or actions. It prints:

- the autoload mode (`off`, `roots`, or `recursive`);

- the active style;
- every consulted path and whether it was found;
- resolved config flags and command-line globals in application order with
  provenance;
- every dropped config line and its reason;
- the style-default table and the effective value for the run;
- the effective safety policy: active safe/dry-run modes, capability decisions and their
  causes, physical source paths/lines and named sections, per-file category interpretation,
  and expanded temp/output roots.

The safety view preserves profile-setting origins even when an unconditional block wins.
See [Inspecting effective policy](design-safety.md#inspecting-effective-policy) for interpretation.

## Known limits

- Config argument quoting supports only the documented braced environment substitutions; it never executes shell syntax.

Directory-scoped temp/output permissions and root-declaration precedence are specified in
[Directory-scoped safety controls](design-directory-safety.md).

## Inactive modifiers in explain output

`--explain` reports `inactive-modifier` notes when a modifier's winning setting came from the CLI
and the effective command has no registered consumer for it. These notes are advisory: reusable
configuration defaults remain valid and quiet, and normal execution is unchanged. A configured
predicate or action counts as a consumer after selector expansion and safety gating. Literal primary
arguments never become modifier requests. A setting superseded later by configuration is no longer
the effective CLI request.

`--match-output` (`-M`) enables default content-line output; `--no-match-output`
(`-M-`) restores the default path listing. Both long forms work in INI configuration.
The short aliases must precede command-line roots. The last setting wins, so `-M-`
can disable a configured match-output default.

The initial registered dependencies cover:

| Modifier                                                                                                      | Consumer                                                                                                                 |
| ------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `--count` / `-c`, `--count-matches`, filename selection, prefix controls, `--only-matching`, `--invert-match` | A `-grep` action or active `--match-output` (`-M`)                                                                       |
| `--context`, `--before-context`, `--after-context`                                                            | `-grep` / `--match-output` line output, or symmetric default context for `-diff`; explicit diff context takes precedence |
| `--diff-context`                                                                                              | Contextual `-diff` output without a per-action count, or tree diff output                                                |
| `--diff-format`                                                                                               | `-diff` without an attached style; tree diffs use unified output                                                         |
| `--diff-ignore`, `--diff-ignore-matching`                                                                     | A `-diff` action                                                                                                         |
| `--diff-algorithm`                                                                                            | A `-diff` action or tree diff output                                                                                     |
| `--shards-show`                                                                                               | Ordinary shard listing, without active summaries or histograms                                                           |
| `--shards-dedup`, `--shard-pattern`                                                                           | Ordinary shard grouping or a `-shard-status` predicate                                                                   |
| `--histogram-width`                                                                                           | Plain or aligned histogram bars                                                                                          |
| `--top`                                                                                                       | An ordinary summary or categorical histogram; comparison-result tables and numeric ranges stay complete                  |
| `--summary-precision`                                                                                         | An active summary or histogram mean                                                                                      |

Hash dependencies inspect the parsed action specs and field templates. `--hash-algorithm`
requires a hash consumer without an explicit algorithm, while `--hash-encoding` requires one
without an explicit encoding. Thus `-hash:sha256` consumes only the encoding default, and
`-hash:/hex` consumes only the algorithm default. The same rules apply to `{hash}` fields.
Post-processing runs after the unqualified hash field and therefore consumes both defaults.

`--summary=hash` consumes both defaults. Cleared summaries, suppressed implicit listings,
grep line templates suppressed by `--count`, literal braces in predicates, escaped printf directives,
and exec arguments without
`--exec-fields` do not provide consumers. Definitions contain literal values, not recursively
parsed templates. Inspection never reads file content.

Archive dependencies also use the effective flavor and linked archive support:

| Modifier                                  | Consumer                                                                                 |
| ----------------------------------------- | ---------------------------------------------------------------------------------------- |
| `--archive-depth`                         | Available `all` or `any` traversal; roots do not nest                                    |
| `--archive-separator`, `--archive-prefix` | Available archive traversal, including roots                                             |
| `--archive-aggregate`                     | Archive traversal plus an ordinary summary, histogram, shard group, or packing operation |

A comparison-result table alone is not an ordinary reduction. Aggregation can also change when
containers are opened before visiting them, so packing and shard grouping count as consumers even
without a summary. The inspection does not open archives. Explicit traversal in a binary without
archive support retains the runtime unsupported-feature error; the xff flavor's implicit roots
mode degrades to no archive traversal in that binary.

`--archive-any` is a mode selector equivalent to `--archive=any`, not a dormant modifier.
Archive write permissions remain policy choices and do not require an immediate action consumer.

The checks use final resolved settings, so `--summary=ext --summary=none` removes that summary's
consumer. For example, `--shards --shards-show=count --summary=ext` has an inactive listing modifier;
adding `--summary=none` restores the shard listing. `--compare=diff --diff-format=y` reports that
`--diff-format` belongs to the per-file action rather than changing the tree-diff renderer.

These are structural dependency checks, not an execution forecast. A consumer behind a predicate
that never matches is still present. Absence of a note does not prove every setting has an effect:
only registered dependencies are checked, and per-value interactions can need more specific rules.
