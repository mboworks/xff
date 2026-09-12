# xff configuration system design

Status: design of record for the shipped configuration system. This document
refines the configuration and safety decisions in [`design.md`](design.md).

## Goals

Configuration should make repeatable command lines convenient without allowing a
file found in a searched repository to execute commands. Resolution must be
inspectable, command-line choices must win, and an explicitly loaded file must
not be able to grant itself permission to run a dangerous directive.

## Sources and precedence

xff has three config-file tiers, followed by the command line:

| Precedence | Source         | Grammar | Trust and purpose                                                  |
| ---------- | -------------- | ------- | ------------------------------------------------------------------ |
| lowest     | `/etc/xff.ini` | INI     | root-owned defaults and authoritative controls                     |
|            | user config    | INI     | trusted personal defaults and named configurations                 |
|            | `--xffrc=FILE` | INI     | explicitly loaded, repeatable, non-arming files                    |
| highest    | command line   | CLI     | explicit flags and selectors; later conflicting values usually win |

There is no project tier. xff never discovers `.xffrc` below a search root or
through its ancestors, and it has no `--project-config` option. Per-directory
`.gitignore`, `.ignore`, and `.xffignore` files are traversal inputs rather than
configuration sources.

The user-config path is selected in this order:

1. non-empty `$XFF_CONFIG`;
2. `$XDG_CONFIG_HOME/xff/config` when `$XDG_CONFIG_HOME` is non-empty;
3. `$HOME/.config/xff/config` when `$HOME` is non-empty.

Only the selected user path is consulted. `/etc/xff.ini`, that user path, and
every explicit `--xffrc` path are reported by `--explain` as found or absent.
The current reader represents missing and unreadable files the same way, so an
unreadable path is reported as absent.

The position-independent `--no-system-config` and `--no-user-config` suppress
their respective automatic tiers; `--no-config` suppresses both. A
present source is still inspected for the permission to suppress it and, for
the system file, its authoritative controls. An explicit command-line `--xffrc=FILE`
remains active: it is not ambient configuration. Ignore files remain
unaffected.

Permission flags (allow and require flags) are config-only directives, not command-line options. `--no-require-*` makes the corresponding file optional so it may be skipped;
`--require-*` prevents skipping a file that exists. Neither requires a missing file to exist.
Without an applicable `--no-require-*`, a present file is required:

The system pair is `--require-system-config` / `--no-require-system-config`;
the user pair is `--require-user-config` / `--no-require-user-config`.

- System pair: unsectioned system config only.
- User pair: unsectioned system or user config; the system decision wins.
- Each pair may appear once per permitted file.
- Neither pair is allowed in named sections, explicit `.xffrc` files, or on the CLI.
- `--no-config` requests both skips and fails if either existing file requires application.

A duplicate system global line is diagnosed and ignored.

`--allow-xffrc` / `--no-allow-xffrc` control acceptance of explicit files. They may occur in system
unsectioned globals or user blocks. A system global denial cannot be overridden by the user file,
a named section, an explicit file, or suppression of system defaults.

Explicit `.xffrc` files cannot contain the require/no-require pairs or
`--allow-xffrc` / `--no-allow-xffrc`; these controls govern automatic files or permission
to load an explicit file, so that file cannot grant itself permission. `--no-allow-exec`
is also prohibited there because it is system-only. In contrast, `--allow-exec` is a
separate runtime flag accepted on the CLI and in config files, but an explicit file's
own `--allow-exec` never arms its dangerous directives.

A requested skip of a present, unauthorized source is a usage error. A missing
source needs no permission because there is no configuration to suppress.

## Shared file grammar

System, user, and explicit `.xffrc` files all accept the same INI grammar: unconditional
options before the first section, then plain named configuration sections. Every section name, including `[global]`, `[defaults]`, and `[policy]`, is an ordinary
named configuration:

```ini
--no-require-system-config
--no-require-user-config
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
not a derived configuration key. An entry written as `--color = auto` is normalized to
`--color=auto`. Blank lines and lines beginning with `#` or `;` are ignored.

One line may contain multiple directives: `--hidden --color=never` or `-name foo -name bar`.
Each primary consumes only its own arguments. Adjacent predicates mean AND, so the latter matches
nothing; use `-name foo -o -name bar` for either name. Parentheses group expressions. Config
predicates and actions are parsed as an expression, then ANDed as a group with the command-line
expression; global options retain their application order. `-type` and `-xtype` accept only the
registered letters `b,c,d,f,l,p,s`, individually or as non-empty comma lists. `-type garbage` is
a usage error on the CLI and invalidates its line or named section in any config file.

Every unconditional line is validated independently. An invalid line is diagnosed with its file,
line number, source text, and command-line validation error, then ignored without suppressing other
unconditional options. A named section is atomic: one invalid line disables the complete section,
so a partially applied configuration is impossible. A section that selects a disabled section with
`--config=NAME` is disabled transitively. Loading continues so independent sections remain usable,
but explicitly selecting any directly or transitively disabled section is a usage error. For
example, `-E development` is invalid because `-E` takes no value; `development` is an unexpected
token, and the containing section is disabled.

There is no special policy rule language. System-only controls belong before the first section;
placing one in a named section disables that section atomically. `--no-allow-exec` is a system-only
global control that prohibits dangerous directives from lower-trust config files.

### Names, refinements, and composition

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

The parser splits flags on spaces and tabs. It does not interpret shell quotes or escapes,
so one token cannot contain whitespace. Blank lines and lines beginning with `#` or `;` are ignored.

User and explicit config files may not redefine the exact built-in preset names `[find]`, `[xff]`,
or `[rg]`. Those lines are dropped with a warning. Use a custom named configuration and compose
the desired preset explicitly with `--config=find`, `--config=xff`, or `--config=rg`.

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

There is no general `--feature` registry. Boolean capabilities use their own
flag family or an existing valued option. A genuinely unsettled spelling may be
gated temporarily by `--unstable=NAME` after its behavior is designed.

## Safety and arming

Registry descriptors classify expression directives as safe, sensitive, or
destructive. A config line receives the most restrictive class of any directive
on that line; that aggregate class drives arming and diagnostics. The trusted unsectioned
`--no-allow-exec` control drops sensitive and destructive lines from user and explicit configs,
even when `--allow-exec` is supplied on the CLI. The prohibition remains authoritative when
system defaults are suppressed. Rejected lines are reported by `--explain` and warned about
during a normal run. Direct CLI expressions and system-authored directives retain their own
runtime safety rules.

System and user configuration are trusted tiers. An explicit `--xffrc` file is a
non-arming tier: naming it authorizes loading it, not executing dangerous content
from it. Sensitive or destructive directives in that tier are dropped unless
`--allow-exec` is active from one of these sources:

- the command line;
- `/etc/xff.ini` defaults;
- an applying user-config line.

An explicit `--xffrc` file is deliberately excluded from that check, so it
cannot authorize itself. The global `--no-allow-exec` prohibition takes precedence over arming. `--allow-exec` only controls config provenance; runtime protections such
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

### Who may skip which file

`/etc/xff.ini`:

```ini
--require-system-config
--no-require-user-config
```

`xff . --no-user-config` is allowed. `--no-system-config` and `--no-config` are rejected.
The combined skip fails because the system file refuses to be skipped. A user-file
`--require-user-config` cannot override the system grant. If the system omits its user-skip
pair, this user config instead decides for itself:

```ini
--no-require-user-config
--color=never
```

All skip controls precede sections. Each pair may occur only once in its file.

`--no-config` requests both `--no-system-config` and `--no-user-config`. Each existing
file must permit its own skip; if either refuses, the entire request fails. A missing file
needs no permission. Without an applicable grant, an existing file cannot be skipped.
To permit all three command-line spellings, put both grants in `/etc/xff.ini`:

```ini
--no-require-system-config
--no-require-user-config
```

### Prevent dangerous directives from lower-trust files

Keep `/etc/xff.ini` administrator-owned and unwritable by users whose configs it constrains:

```ini
--no-allow-exec
```

A user or explicit-file line containing `-exec`, `-capture`, or `-delete` is dropped with a
warning. Even `xff . --allow-exec --xffrc=task.rc` cannot arm dangerous lines in `task.rc`.
Safe config lines remain usable. The prohibition is inspected even when system defaults are
permitted to be skipped; `--allow-exec` inside a named section or user file cannot undo it.

To reject explicit files altogether, add one line:

```ini
--no-allow-exec
--no-allow-xffrc
```

Now `--xffrc=task.rc` is a usage error before `task.rc` is opened, including when a user block
says `--allow-xffrc`.
These controls constrain config-file capabilities; they do not prohibit actions typed directly
on the command line or make a user-controlled executable an operating-system security boundary.
Runtime `--safe`, `--dry-run`, and action-specific confirmation remain separate controls.

### Deliberately arm one explicit file

With no system prohibition, `task.rc` may contain:

```text
-exec echo {} ;
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

No `.xffrc` is loaded automatically from the current directory, search roots, or
ancestors. Each explicit file requires `--xffrc=FILE` and passes the admission and
safety checks described above.

## Resolution and inspection

After discovery, skip authorization, and policy gating, surviving flags are
applied in this order:

1. system defaults;
2. unconditional user-config lines and lines selected by the invocation name;
3. original command-line globals, in their original order;
4. immediately after each `--config=NAME`, system named sections, user and already-loaded explicit
   config lines newly activated by that selector;
5. immediately after each `--xffrc=FILE`, currently applicable lines from that
   file. Later selectors may activate its remaining lines.

Each config line is applied at most once. Consequently, selector placement is
observable when options conflict: in `--color=always --config=plain`, a
`[plain]` section containing `--color=never` wins, while reversing those two CLI
arguments makes `--color=always` win. Multiple explicit files retain their own
positions rather than collapsing into a single tier.

The ordinary option resolvers then apply their documented conflict behavior,
usually last value wins. This preserves provenance, command-line order, and one
set of option semantics.

`--explain` does not walk roots. It prints:

- the active style;
- every consulted path and whether it was found;
- resolved config flags and command-line globals in application order with
  provenance;
- every dropped config line and its reason;
- the style-default table and the effective value for the run.

## Known limits

- xffrc values cannot contain whitespace because quoting is not implemented.
- Missing and unreadable config files are not distinguished in discovery output.
