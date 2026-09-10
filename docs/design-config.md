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
| lowest     | `/etc/xff.ini` | system INI | root-owned defaults and deny policy                                |
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

`--no-config` is absolute and position-independent. It consults none of these
paths, including `/etc/xff.ini` and explicit `--xffrc` arguments, leaving only
built-in defaults and command-line flags. Ignore files remain unaffected.

## File grammars

### System INI

`/etc/xff.ini` recognizes `[defaults]` and `[policy]` sections:

```ini
[defaults]
--color = auto
--jobs = 4

[policy]
user.deny = -delete
xffrc.deny = @sensitive, @destructive
```

A defaults entry becomes one command-line token: `--color = auto` becomes
`--color=auto`, while a bare `--warn` remains bare. Blank lines and lines
beginning with `#` or `;` are ignored.

A policy key is `LAYER.allow` or `LAYER.deny`; its value is a comma-separated
list of exact option/primary names or the class tokens `@safe`, `@sensitive`, and
`@destructive`. The shipped policy engine enforces deny rules for the `user` and
`xffrc` layers. Allow rules remain parse-compatible but are inert because no
existing layer is denied by default. Unknown sections and malformed policy lines
are ignored by the forgiving parser.

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
on that line; that aggregate class drives arming and diagnostics. System
`[policy]` deny rules can reject a line by exact directive name. The class tokens
`@sensitive` and `@destructive` instead match when any directive on the line has
that exact class, including a less restrictive directive on a mixed-class line;
`@safe` matches only a wholly safe line. Rejected lines are dropped, reported by
`--explain`, and warned about during a normal run rather than aborting the entire
invocation.

System and user configuration are trusted tiers. An explicit `--xffrc` file is a
non-arming tier: naming it authorizes loading it, not executing dangerous content
from it. Sensitive or destructive directives in that tier are dropped unless
`--allow-exec` is active from one of these sources:

- the command line;
- `/etc/xff.ini` defaults;
- an applying user-config line.

An explicit `--xffrc` file is deliberately excluded from that check, so it
cannot authorize itself. System policy may still deny a directive after it is
armed. `--allow-exec` only controls config provenance; runtime protections such
as `--safe`, `--dry-run`, and action-specific confirmation remain independent.

## Resolution and inspection

After discovery and policy gating, surviving flags are prepended in this order:

1. system defaults;
2. applying user-config lines;
3. applying explicit `--xffrc` lines, in file and line order;
4. original command-line globals.

The ordinary option resolvers then apply their documented conflict behavior,
usually last value wins. This preserves provenance while keeping one command
parser and one set of option semantics.

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
- System INI parsing is deliberately forgiving; malformed or unknown lines are
  ignored rather than diagnosed.
- `[policy]` allow rules are parsed but currently have no effect because the
  shipped layers are allowed by default; deny rules are authoritative.
- Behavioral-epoch selectors such as `xff:2` are syntactically supported, but
  no migration policy is implied until an epoch is actually defined.
