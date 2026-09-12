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

| Precedence | Source         | Grammar    | Trust and purpose                                                  |
| ---------- | -------------- | ---------- | ------------------------------------------------------------------ |
| lowest     | `/etc/xff.ini` | system INI | root-owned defaults and authoritative controls                     |
|            | user config    | xffrc      | trusted personal defaults and named configurations                 |
|            | `--xffrc=FILE` | xffrc      | explicitly loaded, repeatable, non-arming files                    |
| highest    | command line   | CLI        | explicit flags and selectors; later conflicting values usually win |

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

Permissions are config-only directives, not command-line options:

- `--allow-no-config` / `--no-allow-no-config` govern only `--no-config`;
- `--allow-no-system-config` / `--no-allow-no-system-config` govern only `--no-system-config`;
- those two pairs are system-only, before the first section;
- `--allow-no-user-config` / `--no-allow-no-user-config` govern only `--no-user-config` and may
  appear before the first section in the system or user file; the system decision is authoritative;
- each pair may occur once per permitted file; a duplicate system global line is diagnosed and ignored;
- explicit `--xffrc` files cannot supply these controls.

`--allow-xffrc` / `--no-allow-xffrc` control acceptance of explicit files. They may occur in system
unsectioned globals or user blocks. A system global denial cannot be overridden by the user file,
a named section, an explicit file, or suppression of system defaults.

A requested skip of a present, unauthorized source is a usage error. A missing
source needs no permission because there is no configuration to suppress.

## File grammars

### System INI

`/etc/xff.ini` accepts unconditional options before its first section, plain named configuration
sections. Every section name, including `[global]`, `[defaults]`, and `[policy]`, is an ordinary
named configuration:

```ini
--allow-no-config
--allow-no-system-config
--allow-no-user-config
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
a usage error on the CLI and invalidates its line or named section in the system config.

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

### xffrc

The user config and explicitly named files use a line-oriented, Bazel-rc-style
grammar:

```text
common: --color=auto
debug: --jobs=1 --verbose
xff:debug: --format=jsonl
```

Each non-comment line is:

```text
[BASE[:CONFIG]:] FLAG...
```

`common:` and an omitted selector apply on every run. A named base such as
`debug:` applies when that exact `--config=debug` selector is active. A two-axis
selector such as `xff:debug:` requires both selectors to be active. Matching is
exact against the complete selector string; for example, `xff:2` selects the xff
style by its `xff` prefix but remains a distinct name for xffrc matching.

The parser splits flags on spaces and tabs. It does not currently interpret
shell quotes or escapes, so one token cannot contain whitespace. Blank lines and
lines whose first non-blank character is `#` are ignored.

Config files may not attach defaults directly to the reserved built-in style
blocks `find:`, `xff:`, or `rg:`. Such lines are dropped with a warning so a
plain preset remains reproducible. Use a named block, optionally style-scoped,
and activate it explicitly.

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
--no-allow-no-config
--no-allow-no-system-config
--allow-no-user-config
```

`xff . --no-user-config` is allowed. `--no-system-config` and `--no-config` are rejected.
Allowing the user-only skip does not allow the combined skip. A user-file
`--no-allow-no-user-config` cannot override the system grant. If the system omits its user-skip
pair, this user config instead decides for itself:

```text
--allow-no-user-config
common: --color=never
```

All skip controls precede sections or selector blocks. Each pair may occur only once in its file.

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
common: -exec echo {} ;
```

`xff . --xffrc=task.rc` leaves that dangerous line inert. Supplying `--allow-exec` from the
command line or an applying automatic config permits it through the config gate. Putting
`--allow-exec` in `task.rc` itself never grants that permission. Arming does not bypass the
runtime guards on destructive actions.

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
`plain:` line containing `--color=never` wins, while reversing those two CLI
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
- Behavioral-epoch selectors such as `xff:2` are syntactically supported, but
  no migration policy is implied until an epoch is actually defined.
