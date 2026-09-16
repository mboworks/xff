# xff

eXtended File Find, a find(1)-compatible file finder with modern extensions.

**Usage:** `xff [option...] [path...] [expression]`

## Contents

- [Description](#description)
- [Command structure](#command-structure)
- [Configuration](#topic-config)
- [Safety](#topic-safety)
- [Options](#options)
- [Expression](#topic-expressions)
- [Output](#topic-output)
- [Fields](#topic-fields)
- [Printf directives](#topic-printf)
- [Time formats](#topic-time)
- [Size units](#topic-size)
- [Regex grammars](#topic-grammars)
- [Regex matching](#topic-regex)
- [Content](#topic-content)
- [Comparing trees](#topic-compare)
- [Ignore and VCS traversal](#topic-ignore)
- [Archives](#topic-archive)
- [Statistics](#topic-stats)
- [Environment](#topic-environment)
- [Examples](#topic-cookbook)
- [Exit status](#exit-status)
- [See also](#see-also)

## Description

xff walks each starting path and acts on the entries matching an expression, like `find`(1). With no path it searches the current directory; with no action it prints each match. `xff --compare LEFT RIGHT` instead compares two directory trees as selected status records or a patch.

xff has two flavors selected by the program name: invoked as `find` it restricts the expression to find-compatible primaries, operators, and values; invoked as `xff` it enables the modern extensions. Whole-run xff globals remain available as explicit controls in either flavor. An explicit `--config=find|xff` overrides the program name. Items marked as xff extensions below are the additions over find.

## Command structure

A command consists of whole-run options, zero or more starting paths, and an optional expression. Starting paths come before the expression; the first expression primary begins with a single dash.

- Whole-run double-dash options are position-independent, so `--summary=ext` may appear before the paths or after the expression. They remain literal arguments inside an argument-taking primary such as `-exec` or `-printf`.
- The compatibility globals `-H`, `-L`, `-P`, `-g`, `-j`, and `-z` are leading-only because a single-dash word can otherwise be an expression primary.
- Adjacent tests and actions have an implicit `-a` (AND). Use `!` for NOT, `-o` for OR, and shell-quoted or escaped `(` and `)` for grouping. Evaluation is left to right and short-circuits.
- With no starting path, xff uses `.`. With no explicit action, it prints each matching entry. A bare `--` ends option parsing so a path beginning with `-` can be named unambiguously.

### Basic examples

```sh
xff src -type f -name '*.cc'
```

find regular C++ source files below `src`

```sh
xff . \( -name '*.cc' -o -name '*.h' \) -print
```

group alternatives explicitly; the shell escapes keep the parentheses for xff

```sh
xff --compare left-tree right-tree
```

compare two trees and print only discrepancies

<a id="topic-config"></a>

## Configuration

xff configuration. Options resolve from layered config tiers, then the command line; later layers win. A style (`find` / `xff` / `rg`) sets the baseline defaults, which the tiers and the command line then adjust. Run `--explain` to print exactly what resolved.

### Layers (lowest to highest precedence)

- `system config` - machine-wide defaults (plus root-owned global controls that can prohibit arming)
- `user config` - your personal defaults
- `autoloaded .xffrc` - opt-in root discovery; a NON-ARMING tier before command-line processing
- `--xffrc=FILE` - an explicitly named file (repeatable) - a NON-ARMING tier
- `command line` - flags and `--config`, highest

Autoloading defaults off (`--rc-`). `--rc` discovers `.xffrc` in directory search roots; `--rc+` also discovers descendant files. No ancestor search occurs. `--no-config` disables discovery and excludes named system and user sections. Existing globals remain active by default; the corresponding `--no-require-*-globals` permits excluding them too. Both files are still read and validated. An explicit command-line `--xffrc` remains active.

Globals-requirement directives and the `--allow-xffrc` pair are config-only, not command-line options. Existing globals are required by default; missing automatic files remain normal. The user config is `<OS account home>/.config/xff/config`, independent of environment variables. Explicit `--xffrc=FILE` must exist. Existing unreadable configs and dangling symlinks are errors, even when a skip is requested. The system pair `--require-system-globals` / `--no-require-system-globals` is allowed only in unsectioned system globals, once per pair. The user pair `--require-user-globals` / `--no-require-user-globals` is allowed in unsectioned system or user globals, once per pair per file; the system decision wins, even if its own globals are skipped. Required globals stay active while named sections are excluded; optional globals are also excluded when the corresponding skip is requested. A permission alone does not skip anything. `--no-config` requests both exclusions and disables autoloading. Named sections remain optional; selecting a name unavailable in every active file is an error. Neither the CLI nor named sections or `.xffrc` files may set these requirement directives. Explicit `--xffrc` files may not contain any of these controls. Separately, `--allow-xffrc` / `--no-allow-xffrc` is a normal config-only setting usable in system defaults or any user config block; user selection and precedence decide whether command-line `--xffrc=FILE` is accepted, and an unsectioned system denial cannot be overridden. Admission is checked before any `.xffrc` discovery or explicit paths are opened. The admission pair also governs autoloading. User decisions follow ordinary application order, including in-place `--config=NAME` composition; INI section order never overrides explicit selector order. Each section applies once.

```ini
[deny]
--no-allow-xffrc
[allow]
--allow-xffrc
```

With that user INI, `xff . --config=allow --config=deny --xffrc=task.rc` rejects loading before opening `task.rc`; reversing the selectors permits loading. The same rule applies to `--rc` and `--rc+`.

System, user, and all `.xffrc` files share one INI grammar. Write unconditional options with their exact command-line spelling before the first section, and named configurations as plain `[NAME]` sections. For example, write `--color=auto`, then `[dev]` and `-E`; do not remove the option dashes or add a `config` section prefix. Global lines are validated and an invalid one fails the run, including when a trusted file is requested to be skipped. Named sections are atomic: one invalid line disables the entire section. A name may be declared only once per file, including empty sections; duplicate declarations disable that name. Other files may refine the same name. Disablement propagates through `--config=NAME` references, and selecting a disabled section is a usage error. Thus `-E development` disables its section because `-E` takes no value and `development` is an unexpected token.

A line may contain multiple directives: `--hidden --color=never` or `-name foo -name bar`. Adjacent predicates mean AND; use `-name foo -o -name bar` for either name. Config predicates and actions form an expression that is ANDed as a group with the CLI expression. Each primary consumes only its own arguments. Closed choices are validated by the shared CLI parser: `-type garbage` is invalid, while `-type f,d` is a valid any-of list.

Config files emulate the shell's quoting and escaping to produce arguments for the same CLI parser. The config reader removes comments before passing those arguments to the parser; config files are not shell scripts. System, user, and all `.xffrc` files all use this rule. Single or double quotes group arguments, including spaces and empty values; outside quotes, a backslash escapes the next character. Inside double quotes, backslash escapes only double quote, backslash, dollar, backtick, or newline. There is no variable, command, pathname, or tilde expansion. Use the same flag spelling as on the CLI: `--color=auto`, not `--color = auto`. Whitespace separates arguments; it is not assignment syntax.

An unquoted `#` at the beginning of a word starts a comment through the end of the physical line. `foo#bar`, `\#`, and `"#"` are literal arguments. An unquoted, unescaped `;` starts a comment anywhere outside quotes. Both comment markers may follow any amount of whitespace. Quote or escape literal semicolons: `\;` or `";"` supplies the `-exec` terminator, just as on the CLI. For example, `-exec echo x \; ; comment` contains an exec action followed by a comment. Likewise, `-name '#*' # Match names beginning with a hash` contains one predicate and a comment.

Quotes may span lines. Backslash-newline continues a logical line without adding a character, except inside single quotes. An unmatched quote or trailing backslash invalidates the logical line, with a diagnostic at its starting line number; no partial arguments are applied.

### Config-only controls

These directives are accepted only inside the stated system/user config files, not on the command line or in any `.xffrc` file. An allow/deny pair is one setting: where a pair is limited to one occurrence, its positive and negative forms may not both appear.

The system pair is `--require-system-globals` / `--no-require-system-globals`; the user pair is `--require-user-globals` / `--no-require-user-globals`.

- System pair: unsectioned system config only.
- User pair: unsectioned system or user config; the system decision wins.
- Each pair may appear once per permitted file.
- Neither pair is allowed in named sections, explicit `.xffrc` files, or on the CLI.
- `--no-config` excludes both sets of named sections, retains required globals, and disables rc autoloading.

- `--no-require-system-globals` - permits skipping system globals with `--no-system-config` or `--no-config`; system config only, before the first section, and at most one of this pair
- `--require-system-globals` - keeps system globals active, including with `--no-config`; system config only, before the first section, and at most one of this pair
- `--no-require-user-globals` - permits skipping user globals with `--no-user-config` or `--no-config`; before the first section in the system or user config, and at most one of this pair per file; the system decision is authoritative
- `--require-user-globals` - keeps user globals active, including with `--no-config`; before the first section in the system or user config, and at most one of this pair per file; the system decision is authoritative
- `--allow-xffrc` - allows automatic discovery and command-line `--xffrc=FILE`; usable in system defaults or any user config block, with normal config selection and last-value precedence
- `--no-allow-xffrc` - denies automatic discovery and command-line `--xffrc=FILE`; usable in system defaults or any user config block, with normal config selection and last-value precedence; an unsectioned system denial is authoritative
- `--allow-rc-globals` - permits unsectioned content in autoloaded `.xffrc` files; once per pair before sections in system/user INI; a system denial wins, otherwise the applying user choice wins
- `--no-allow-rc-globals` - rejects autoloaded files containing unsectioned content before actions run (default); same scope and precedence as the positive form; explicit `--xffrc=FILE` is unaffected
- `--block-policy-categories=LIST` - selects the interpretation of blocks in this file, including its named sections; once before sections in system or user config; see `--help=safety` for the operation table

Neither explicit nor autoloaded `.xffrc` files may contain the require/no-require pairs, `--allow-xffrc`, `--no-allow-xffrc`, the rc-globals permission pair, or `--block-policy-categories`. The separate runtime flag `--allow-exec` is accepted on the CLI and in config files, but an explicit file's own setting never arms its dangerous directives.

### Tiny config examples

Defaults and a named override in `/etc/xff.ini`: `xff . --config=dev` ends with `--color=never`; adding `--color=always` after the selector overrides it.

```ini
--color=auto
[dev]
--color=never
```

Refine `[dev]` across files: the system file supplies `--color=auto`, the user file supplies `--color=always`, and `task.xffrc` supplies `--color=never`. `xff . --config=dev --xffrc=task.xffrc` ends with `--color=never`. Each file declares `[dev]` only once.

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

`--config=checks` composes configurations; repeated references are allowed and each contributing line applies at most once. Later-file refinements wait for the earlier file's section to finish. Section names are literal: `[common]` is named, and `[xff:debug]` matches only `--config=xff:debug`. Unconditional flags go before the first section.

An explicit `task.xffrc` uses exactly the same INI format. Its unsectioned flags apply when loaded; named flags apply only when that configuration is selected:

```ini
--hidden

[quiet]
--color=never
```

`xff . --xffrc=task.xffrc` applies `--hidden`; adding `--config=quiet` also applies `--color=never`. The invocation name or another config's `--config=quiet` can also select the section. A selector alone does not discover files. Unsectioned `--config=NAME` directives can select sections when the file loads. Autoloading is separately enabled with `--rc` or `--rc+`.

Keep system globals mandatory while permitting user globals to be skipped. These system controls override the user file's own globals requirement.

```ini
--require-system-globals
--no-require-user-globals
```

`xff . --no-config` retains system globals, excludes user globals, excludes both sets of named sections, and disables autoloading. To permit excluding globals from both files, use:

```ini
--no-require-system-globals
--no-require-user-globals
```

To prohibit execution regardless of its source, require globals in an administrator-owned system file and set `--block-execution`. Add `--no-allow-xffrc` to reject explicit files altogether:

```ini
--require-system-globals
--block-execution
--no-allow-xffrc
```

Neither user `--allow-xffrc` nor an explicit file's own `--allow-exec` can undo those prohibitions. Execution blocks also constrain actions typed directly on the CLI. Runtime `--safe`, `--dry-run`, and action-specific confirmation remain separate. Without a system prohibition, `--allow-exec` from the CLI or an applying automatic config permits dangerous explicit-file lines through the config gate; the explicit file cannot arm itself.

### Choosing a style

Every `--config=NAME` remains active, so multiple named blocks can apply. Among built-in style selectors, the last `find`, `xff`, or `rg` selects the baseline; custom names do not change it. See `--help=styles` for the table. The invocation name (`argv[0]`) is the leading selector, so a symlink named `find` selects the find expression style and `rg` the rg style; any other name (e.g. a `mytool` symlink) activates a same-named config block over the xff default. Explicit `--config` selectors stack on top.

Configuration expands in application order. Automatic system/user defaults and the invocation selector come first; each command-line `--config` then activates newly matching lines at that exact position, and each `--xffrc` loads its currently matching lines where it appears. Because conflicting options usually use the last value, moving a selector can intentionally change the result. A config line is applied at most once.

Within one logical config section, repeating an overriding setting keeps the normal last-value-wins result but emits a warning: the earlier value is locally redundant and is usually a copy/paste mistake. Options that deliberately accumulate (such as `--exclude`, `--summary`, and `--define`) may repeat without warning, as may expression primaries. Separate config sections and tiers remain independent. Command-line repetition is never warned about, so a pasted command can be adjusted by appending an override.

### Autoloading .xffrc files

- `--rc-` - disable automatic loading (default); explicit `--xffrc=FILE` remains active
- `--rc` - load `.xffrc` directly in each directory search root
- `--rc+` - also load `.xffrc` throughout descendant directories

Modes may be set on the CLI or in applying system/user configuration; the last setting wins. `--no-config` suppresses all discovery regardless of mode order. An `.xffrc` cannot set its own discovery mode. With no root argument the default root is `.`; file roots do not load a parent's configuration. There is no ancestor search, directory-symlink following, or discovery inside archives. Discovery visits physical directories before expression execution, independently of ignore files, hidden-file settings, depth limits, and pruning. Recursive mode can therefore inspect a large tree, even for `--explain`, which performs discovery but never evaluates actions.

Each file affects the whole invocation, not just its subtree. Argument roots are processed in order; within a tree, parents precede descendants and sibling names are sorted. Overlapping or repeated roots do not automatically load the same directory twice. Discovered files follow system/user defaults and precede CLI flags. Named sections retain the shared `--config=NAME` composition and selection rules. Explicit files retain their CLI positions; explicitly naming a discovered file applies it again there. Relative flag values remain relative to the working directory, as in every other INI file.

By default an autoloaded file may contain only named sections. The config-only permission pair `--allow-rc-globals` / `--no-allow-rc-globals` may appear once before sections in each system or user INI. A system denial is authoritative; otherwise an applying user decision overrides the system default. Neither named sections, `.xffrc` files, nor CLI arguments may set this permission. Explicit `--xffrc=FILE` is unaffected. When globals are forbidden, any unsectioned directive fails loading before actions run: this includes expression primaries and `--config=NAME`, not only double-dash options. Nothing is silently discarded, because doing so could remove an intended restriction.

```ini
# project/.xffrc: no global permission needed
[checks]
-type f
```

`xff project --rc --config=checks` selects the shared file-only profile.

```ini
# user INI, before all sections
--rc
--allow-rc-globals
```

```ini
# project/.xffrc
--hidden
[quiet]
--color=never
```

With that grant, `xff project` applies `--hidden`, and `--config=quiet` also applies `--color=never`. Without the grant the unsectioned `--hidden` causes an error. Missing `.xffrc` files are normal; unreadable files/directories and non-regular config files fail discovery. `--explain` reports the mode and every consulted config path. Autoloading and the globals grant never arm dangerous actions; trusted `--allow-exec` and mandatory `--block-*` restrictions keep their existing meanings. Ordinary options can still change results, so granting globals is a deliberate trust decision.

### Arming dangerous directives

A dangerous directive (the exec family `-exec` / `-execdir` / `-ok` / `-capture`, or `-delete`) carried by an explicit or autoloaded `.xffrc` file is inert unless `--allow-exec` is set from a trusted tier (the command line or the system/user config, never an `--xffrc` file itself). Unarmed lines are dropped with a warning. Arming cannot bypass unconditional safety blocks or an active safe profile.

See also: [Safety](#topic-safety), [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="topic-safety"></a>

## Safety

Without configured restrictions, operations are allowed. `--block-*` restrictions accumulate and cannot be cleared by later settings. `--safe` activates the configurable profile; `--no-safe` deactivates that profile without clearing unconditional blocks. Every `--safe-block-*` has a `--no-safe-block-*` counterpart; these profile settings use the last applied value. Initially the profile blocks every relevant capability. Activating it does not reset its definition.

`--block-policy-categories=LIST` selects categories that use dedicated controls. The list is comma-separated; an empty list (the default) uses ordinary file controls throughout. `archive`, `temp`, and `output` are supported categories. Unknown categories are errors. This config-only directive may occur once in the unsectioned system config and once in the unsectioned user config; each file chooses its own policy. Named sections, explicit `.xffrc` files, and the CLI cannot set it. Keep policy globals active with `--require-system-globals` or `--require-user-globals` when users must not skip them.

Each file's directives are translated before composition. Selecting `archive` in another file cannot remove mandatory restrictions already imposed. Select `archive` for precise archive control; an empty list is the simple default. CLI file controls cover archives as well. Every control listed in a table cell must permit the operation. Names omit their prefixes: `file-writing` means both `--block-file-writing` and, when safe mode is active, `--safe-block-file-writing`. `execution` independently controls arbitrary command execution. The table defines permissions; it does not enable archive operations or add unsupported member-editing actions.

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


`--temp-root=PATH` and `--output-root=PATH` are config-only, once per unsectioned system or user INI. The system declaration wins. Roots must be existing absolute directories without symlink components; use physical paths (for example `/private/tmp` on macOS). Permissions cover all descendants, including subdirectories, but never deletion or replacement of the root itself. Root declarations do not redirect output filenames. The temp root also selects extraction and mount scratch placement; environment variables cannot grant a directory exception.

| Operation                         | Ordinary path                | temp selected                          | output selected                            |
| --------------------------------- | ---------------------------- | -------------------------------------- | ------------------------------------------ |
| Create file                       | file-writing                 | temp-file-writing                      | output-file-writing                        |
| Overwrite file                    | file-writing, file-overwrite | temp-file-writing, temp-file-overwrite | output-file-writing, output-file-overwrite |
| Delete file or symlink            | file-deletion                | temp-file-deletion                     | output-file-deletion                       |
| Create directory                  | directory-creation           | temp-directory-creation                | output-directory-creation                  |
| Delete empty directory            | directory-deletion           | temp-directory-deletion                | output-directory-deletion                  |
| Recursive deletion                | Check every entry            | Check every entry                      | Check every entry                          |
| Delete or replace a declared root | Prohibited                   | Prohibited                             | Prohibited                                 |


Unselected directory categories inherit ordinary controls through per-INI expansion. Overlapping temp/output scopes require both sets of permissions. Explicit archive restrictions also apply to archives within directory scopes. With declared roots, archive output outside all roots also needs ordinary file writing/overwrite permission. Select `archive` alongside directory categories to allow archives inside them while blocking ordinary writes elsewhere. Scoped paths reject parent traversal (`..`) and symlink traversal. Scoped overwrite replaces the directory entry rather than modifying a shared hard-link inode. Ordinary extraction and mount scratch require writing and directory-creation permission. Archive-owned staging is part of an authorized archive write, remains on the destination filesystem for archive publication, and is cleaned through retained handles. It does not grant access to other pre-existing temporary files. User-directed directory creation and deletion have separate controls.

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

With those existing roots, this system policy permits new output and scratch work while prohibiting ordinary writes, ordinary deletion, execution, and overwrite/deletion of output contents. An unselected category in another mandatory file can impose additional restrictions. To prohibit an operation everywhere, leave its categories unselected when imposing the ordinary block, or explicitly block every dedicated category as well.

Packing a new archive includes building its contents; member-editing controls do not apply. Replacing an entire archive replaces all its contents, regardless of member-editing restrictions. To preserve existing archives, block archive overwrite. Archive authorization covers only the selected archive output and its necessary owned temporary files, never extraction or unrelated writes. When overwrite is blocked, creation must atomically refuse an existing destination, including symlinks.

```ini
--require-system-globals
--block-policy-categories=archive
--block-execution
--block-file-writing
--block-file-deletion
--block-directory-creation
--block-directory-deletion
--block-archive-overwrite
```

This system config allows packing new archives while blocking ordinary writes, deletion, execution, and replacement of existing archives. Child processes are not sandboxed by xff's file controls; block execution when those controls must not be bypassed by a command.

Explicit-file arming with `--allow-exec` covers execution and deletion. It does not gate file or archive output: an unarmed explicit file can request writes and overwrites when this safety policy permits them. Use unconditional blocks for mandatory output restrictions.

`--dry-run` validates safety restrictions first, then previews actions without executing commands or modifying files. It never grants a blocked operation. Reading and normal terminal output remain allowed. A command's exit status or captured output cannot be predicted: preview stops evaluation of that entry and reports an incomplete preview rather than inventing subsequent matches.

See also: [Configuration](#topic-config), [Archives](#topic-archive), [Output](#topic-output)

## Options

### Config

<a id="flag-config"></a>

- `--config=NAME` - activate a named config or select the find, xff, or rg style; repeatable _(global, xff)_
  A config style sets the defaults for ignore files, hidden files, sizes, sort order, and case. find restricts the expression to find-compatible vocabulary and defaults; whole-run xff globals remain available as explicit overrides. xff keeps find's grammar but sorts and prints human sizes; rg is opinionated (respect `.gitignore`, skip hidden, smart case). Every occurrence remains an active selector, so several named config blocks can apply. All config files use INI sections; each name is declared once per file and may be refined in other files. `--config=NAME` inside a section composes it with another config. Among the built-in style selectors, the last `find`, `xff`, or `rg` occurrence chooses the baseline; custom names do not change it. A `STYLE:EPOCH` spelling such as `xff:2` selects `STYLE` while retaining the full name as a config selector, which must be declared in an active config file. Only plain `find`, `xff`, and `rg` need no declaration. See `--help=styles` for the per-style defaults and `--help=config` for layering.
  See also: [Configuration](#topic-config)

<a id="flag-require-system-globals"></a>

- `--require-system-globals` - keep existing system globals active when named configs are skipped _(global, xff, config-only)_
  Config-only: unsectioned system config only, once per positive/negative pair. System globals remain active with `--no-system-config` or `--no-config`. This is the default; missing files remain normal. Named sections can still be excluded.
  See also: [Configuration](#topic-config)

<a id="flag-no-require-system-globals"></a>

- `--no-require-system-globals` - permit skipping system globals along with named configs _(global, xff, config-only)_
  Config-only: unsectioned system config only, once per pair. `--no-system-config` or `--no-config` then excludes globals as well as named sections. Without a skip request, globals still apply. Authoritative system admission and requirement controls are still inspected.
  See also: [Configuration](#topic-config)

<a id="flag-require-user-globals"></a>

- `--require-user-globals` - keep existing user globals active when named configs are skipped _(global, xff, config-only)_
  Config-only: unsectioned system or user config, once per pair per file. The system decision wins. User globals remain active with `--no-user-config` or `--no-config`. This is the default; missing files remain normal. Named sections can still be excluded.
  See also: [Configuration](#topic-config)

<a id="flag-no-require-user-globals"></a>

- `--no-require-user-globals` - permit skipping user globals along with named configs _(global, xff, config-only)_
  Config-only: unsectioned system or user config, once per pair per file. The system decision wins. `--no-user-config` or `--no-config` then excludes user globals as well as named sections. Without a skip request, globals still apply.
  See also: [Configuration](#topic-config)

<a id="flag-allow-xffrc"></a>

- `--allow-xffrc` - permit explicit and automatic .xffrc loading _(global, xff, config-only)_
  Config-only: unsectioned system config or any user section. User decisions follow configuration application order, including composed sections. A system denial remains authoritative. Neither the CLI nor an .xffrc file may grant admission.
  See also: [Configuration](#topic-config)

<a id="flag-no-allow-xffrc"></a>

- `--no-allow-xffrc` - reject explicit and automatic .xffrc loading _(global, xff, config-only)_
  Config-only: unsectioned system config or any user section. A system denial cannot be overridden by user or .xffrc content, or by skipping system defaults. Admission is checked before files are opened.
  See also: [Configuration](#topic-config)

<a id="flag-no-config"></a>

- `--no-config` - exclude named system and user configs and disable rc autoloading _(global, xff, command-line-only)_
  Command-line only; rejected in configuration files.
  Excludes named system and user sections and disables `.xffrc` autoloading regardless of flag order. Existing globals remain active by default; the applicable `--no-require-system-globals` or `--no-require-user-globals` permits excluding the corresponding globals too. Both files are still read and validated. Missing automatic files remain normal. Explicit `--xffrc=FILE` inputs and ignore files remain active.
  See also: [Configuration](#topic-config)

<a id="flag-no-system-config"></a>

- `--no-system-config` - exclude named system config sections while retaining required globals _(global, xff, command-line-only)_
  Command-line only; rejected in configuration files.
  Excludes named sections in `/etc/xff.ini`. Unsectioned globals remain active by default or with `--require-system-globals`; `--no-require-system-globals` allows excluding them too. The file is still read and validated. User and explicit `.xffrc` configurations remain active.
  See also: [Configuration](#topic-config)

<a id="flag-no-user-config"></a>

- `--no-user-config` - exclude named user config sections while retaining required globals _(global, xff, command-line-only)_
  Command-line only; rejected in configuration files.
  Excludes named user sections. Unsectioned globals remain active by default or with `--require-user-globals`; `--no-require-user-globals` allows excluding them too. The system decision takes precedence over the user declaration. The file is still read and validated. System and explicit `.xffrc` configurations remain active.
  See also: [Configuration](#topic-config)

<a id="flag-rc"></a>

- `--rc[-|+]` - discover .xffrc in argument roots; minus disables, plus includes descendants _(global, xff)_
  Defaults to `--rc-` (off). `--rc` loads `.xffrc` in each directory search root; `--rc+` also searches descendant directories. With no roots, the default root is `.`. Discovery finishes before execution, in argument order, parent before children and lexicographically among siblings. Files contribute to the whole invocation, after trusted defaults and before CLI flags. Discovery ignores search filters and never follows directory symlinks or enters archives. Only system/user configuration and the CLI may set this mode. `--no-config` disables discovery. Unsectioned content requires a trusted `--allow-rc-globals` grant; otherwise loading fails. Explicit `--xffrc=FILE` remains independent.
  Affects: --xffrc, --explain
  Affected by: --allow-rc-globals, --no-allow-rc-globals
  See also: [Configuration](#topic-config), [--allow-rc-globals](#flag-allow-rc-globals), [--no-allow-rc-globals](#flag-no-allow-rc-globals), [--xffrc](#flag-xffrc), [--explain](#flag-explain)

<a id="flag-allow-rc-globals"></a>

- `--allow-rc-globals` - permit unsectioned content in automatically discovered .xffrc files _(global, xff, config-only)_
  Config-only: once per pair in unsectioned system/user configuration. A system `--no-allow-rc-globals` decision cannot be overridden. Without a grant, an autoloaded file containing unsectioned content fails before execution. Named sections remain available; explicit `--xffrc=FILE` is unaffected. This permission does not arm dangerous actions.
  Affects: --rc
  See also: [Configuration](#topic-config), [--rc](#flag-rc)

<a id="flag-no-allow-rc-globals"></a>

- `--no-allow-rc-globals` - reject unsectioned content in automatically discovered .xffrc files _(global, xff, config-only)_
  The default without an explicit grant. Config-only: once per pair before all sections in system/user configuration. A system denial is authoritative. Rejection includes unsectioned expressions and `--config=NAME`; nothing is silently dropped. Explicit `--xffrc=FILE` keeps its existing global and named-section behavior.
  Affects: --rc
  See also: [Configuration](#topic-config), [--rc](#flag-rc)

<a id="flag-xffrc"></a>

- `--xffrc=FILE` - also load a specific config file (a non-arming tier; see --allow-exec) _(global, xff, command-line-only)_
  Command-line only; rejected in configuration files.
  Loads FILE as a config tier above the user config (naming it is consent to LOAD it). It is a NON-ARMING tier: execution and deletion actions - the exec family (-exec/-execdir/-ok, -capture) or -delete - are inert unless --allow-exec is set from a trusted tier (the CLI or the user/system config, never from an --xffrc file itself). An unarmed dangerous line is dropped with a one-line warning. Repeatable; later files win.
  Affects: --allow-exec
  Affected by: --rc, --allow-exec
  See also: [Configuration](#topic-config), [--rc](#flag-rc), [--allow-exec](#flag-allow-exec)

<a id="flag-allow-exec"></a>

- `--allow-exec` - arm dangerous directives from explicit or autoloaded .xffrc files _(global, xff)_
  Permits the sensitive/destructive directives (the exec family -exec/-execdir/-ok and -capture, and the destructive -delete) carried by an explicit or autoloaded `.xffrc` file to actually run. Honored only from a trusted tier - typed on the CLI, or set in the user/system config - never from an --xffrc file (so a named config cannot authorize itself). Arming cannot bypass unconditional blocks or the active safe profile; see `--help=safety`.
  Affects: --xffrc
  Affected by: --xffrc
  See also: [Configuration](#topic-config), [--xffrc](#flag-xffrc)

<a id="flag-explain"></a>

- `--explain` - print the resolved configuration and exit _(global, xff, command-line-only)_
  Command-line only; rejected in configuration files.
  Prints the active style, every config source consulted and whether it was found, resolved flags in application order with their provenance, rejected config directives, and the style-default table with this run's effective values. It performs enabled `.xffrc` discovery but does not evaluate the expression. Existing unreadable config files and missing explicit `--xffrc` files are errors.
  Affected by: --rc
  See also: [Configuration](#topic-config), [--rc](#flag-rc)

### Matching

<a id="flag-e"></a>

- `-E` - use the configured extended-regex grammar (RE2 by default) _(global, find)_
  Accepts BSD/macOS find's leading extended-regex switch. In xff the extended grammar is selected by `--regextype`; it defaults to `RE2`, and command-line `--re2` or `--pcre` override a configured choice. This compatibility flag does not itself replace that choice.
  Affects: -regex, -iregex, -rxc, -irxc, -grep, -capture, -capturedir
  See also: [Regex matching](#topic-regex), [Regex grammars](#topic-grammars), [-regex](#primary-regex), [-iregex](#primary-iregex), [-rxc](#primary-rxc), [-irxc](#primary-irxc), [-grep](#primary-grep), [-capture](#primary-capture), [-capturedir](#primary-capturedir)

### Traversal

<a id="flag-h"></a>

- `-H` - follow symlinks named on the command line, not while walking _(global, find)_
  Dereferences each symlink root operand before matching or descending, but keeps symlinks found below that root as symlinks. A dangling root symlink falls back to the link itself. `-H`, `-L`, and `-P` are mutually overriding leading options; the last occurrence wins.
  See also: [Configuration](#topic-config), [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="flag-l"></a>

- `-L` - follow symlinks everywhere during the walk _(global, find)_
  Dereferences symlink roots and symlinks encountered below them. Matching sees the target's type and metadata, and directory targets are descended. Dangling links fall back to the link itself. Filesystem loops are detected and reported instead of recursed indefinitely. `-H`, `-L`, and `-P` are mutually overriding leading options; the last occurrence wins.
  See also: [Configuration](#topic-config), [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="flag-p"></a>

- `-P` - never follow symlinks (the default) _(global, find)_
  Matches every symlink as a link and never descends through it, including a symlink supplied as a root operand. Predicates that explicitly inspect a target, such as `-xtype` and `-lname`, retain their documented behavior. `-H`, `-L`, and `-P` are mutually overriding leading options; the last occurrence wins.
  See also: [Configuration](#topic-config), [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

### Archive traversal

<a id="flag-archive"></a>

- `--archive[=none|roots|all|any], -z[-|+|++], -Z[-|+|++]` - descend into archives: -z- none, -z roots only, -z+ / bare --archive all _(global, xff)_
  One of:

  - `none` - an archive is one plain file (find behavior; the find-style default)
  - `roots` - dive only when a search root is itself an archive (the xff-family default)
  - `all` - also dive archives found during the walk (what bare `--archive` selects)
  - `any` - `all`, plus offer EVERY file to the reader, not only container-looking names

  Treats each archive (tar, gz, bzip2, xz, zstd, lz4, zip, ...) as a directory, so a member is an ordinary entry at a member path like `foo.tar.gz!inner/x` and the expression matches it with the same -name / -type / -size / -newer every other entry gets - and the predicates and fields that READ an entry (-grep, -content, -hash, {hash}, {lines}) read the member out of its container. The modes are nested: `none` keeps find's behavior (an archive is one plain file); `roots` dives only when a search root is itself an archive (pointing xff AT an archive implies looking inside); `all` also dives archives discovered during the walk; `any` is `all` without the name gate, offering every file to the reader (the older spelling is `--archive-any`). Bare `--archive` means `all`, and the short form carries chmod-style suffix signs (`-z-` none, `-z` roots, `-z+` all, `-z++` any). The UPPER-case family is the same ladder with writing armed (`-Z` is `-z` plus `--archive-write`, `-Z+` is `-z+` plus it, `-Z++` is `-z++` plus it): the case carries the capability and the signs carry the level, so aiming at one cannot reach the other. The two axes resolve independently and later wins, so `-Z++ -z-` arms writing with reading off, while `-Z-` is the full reset (reading off AND writing disarmed). The find style defaults to `none`, every xff-family style to `roots`. Members are read-only until a write spelling arms them, so `-delete` and the exec family refuse them rather than silently skipping. Under `all`, a file met mid-walk is offered to the reader only if its NAME looks like a container (`any` drops that gate); one named on the command line always is. A native phar exposes its executable stub as `.phar/stub.php`, matching tar- and zip-based phars; it is an ordinary regular member for matching, fields, and statistics. If the manifest stores that path, the stored member wins so one path never denotes two entries. The synthetic stub is readable but cannot be deleted because the format requires it. build-time extras: the stock binary is lean and omits readers; rebuild with `--config=xff_full`, `--//xff:xff_archive` for the broad libarchive-backed set, or one of the independent `--//xff:xff_asar` or `--//xff:xff_squashfs` readers. Asking for archive handling without any reader is a hard error.
  Affected by: --archive-depth, --archive-aggregate, --archive-delete, --archive-extract, --archive-mount, --archive-any
  See also: [Archives](#topic-archive), [--archive-depth](#flag-archive-depth), [--archive-aggregate](#flag-archive-aggregate), [--archive-delete](#flag-archive-delete), [--archive-extract](#flag-archive-extract), [--archive-mount](#flag-archive-mount), [--archive-any](#flag-archive-any)

<a id="flag-archive-depth"></a>

- `--archive-depth=N` - how many containers deep --archive dives (default 1) _(global, xff)_
  Counted in CONTAINERS, not directory levels: the default 1 opens an archive but leaves an archive INSIDE it a plain member, so a `.gem` shows its `data.tar.gz` without unpacking it. `--archive-depth=2` opens that one too. Its own knob rather than part of -maxdepth because nesting is where a decompression bomb lives - a few kilobytes can promise gigabytes per level - while -maxdepth keeps counting member levels as the ordinary depth they are. Only `all` nests: under `roots` a member is never a search root, so nothing inside the container is dived whatever the value. N must be at least 1; use --archive=none / -z- to stop diving.
  Affects: --archive
  See also: [Archives](#topic-archive), [--archive](#flag-archive)

<a id="flag-archive-aggregate"></a>

- `--archive-aggregate=<MODE>` - what --summary / --histogram count when the walk dives (default members) _(global, xff)_
  MODE is one of:

  - `members` - count what is INSIDE a dived container, not the container (the default)
  - `container` - count containers as the files they are on disk, never their members
  - `both` - count each container AND its members - the archive plus its unpacked copy

  Diving makes one byte visible twice - once as the container's own size, once as its members' - so a total that adds `both` describes no filesystem that exists. `members` (the default) counts a dived container's members instead of the container itself, which is what unpacking it and measuring the result would give; `container` counts the archives and never what is in them, which is what the disk holds; `both` counts everything, the archive AND its unpacked copy, for when the doubling is the point. Only the REDUCTIONS are affected: -print and every action still see every entry the walk visits, so a member is listed under `container` and the container is listed under `members`. `members` needs the walk to open a container before deciding, so a `-prune` on a container no longer avoids opening it - use another mode, or no reduction, to keep that.
  Affects: --archive
  See also: [Archives](#topic-archive), [--archive](#flag-archive)

<a id="flag-archive-delete"></a>

- `--archive-delete` - let -delete remove an archive member, rewriting its container _(global, xff)_
  There is no such thing as removing a member in place: an archive is a stream of header and data records, so the container is written again from the members that survive. That is why this is opt-in and why `-delete` refuses a member without it - an action that silently rewrites a whole archive is not one to do by default. The rewrite happens after the walk, once per container however many of its members matched, because the walk is reading that same container while it runs. The new archive keeps the original's format and compression (a `.tar.gz` stays a gzipped tar) and every surviving member keeps its name, mode, times and content; it is written beside the original and renamed over it only when complete, so an interrupted run leaves the container as it was. `--dry-run` lists the members that would go and writes nothing. A NATIVE phar is rewritten too, by xff's own writer: the manifest and data section are rebuilt from the surviving entries verbatim (so per-member gz / bz2 compression is untouched) and the trailing signature is recomputed (md5 / sha1 / sha256 / sha512). Refused, with the reason named: a format this build reads but cannot write (7-Zip, RAR, ISO); a TAR-based or ZIP-based phar, whose signature is a MEMBER computed over the rest of the container, so a rewrite would leave it stale and PHP would reject the result; an OpenSSL-signed phar, which cannot be re-signed without its private key; a compressed single file, which has no member list to rewrite; and a member of a container nested inside another one.
  Affects: --archive
  Affected by: --archive-write
  See also: [Archives](#topic-archive), [--archive-write](#flag-archive-write), [--archive](#flag-archive)

<a id="flag-archive-extract"></a>

- `--archive-extract` - let -exec / -ok run on an archive member, via a temporary copy _(global, xff)_
  A member is bytes inside a container, so there is no path a child process can open and the exec family refuses one by default. With this flag the member is written to its own temporary directory under the same name it has inside the archive, and the child is handed THAT path: `{}` renders as the temporary file, `-execdir` runs in the temporary directory, and `-ok` shows the copy in its prompt before anything runs. Each copy is removed as soon as its child finishes (for a `+` batch or a `-j` child, when the run ends), so nothing is left behind. The copy first goes to a memory-backed directory where the platform has one (`$XDG_RUNTIME_DIR` or `/dev/shm` on Linux, both tmpfs), so the child gets an ordinary path without normally writing the member to disk. If no memory-backed directory is available, or the member is larger than its reported free space, xff falls back to `$TMPDIR` (or the platform temporary directory); extraction therefore does not guarantee that member data never reaches disk. It is opt-in because the child is editing a COPY: a formatter or a patch tool will report success and change nothing in the archive. `-delete` stays refused whatever this flag says - removing a temporary copy would be a no-op dressed as a deletion. The container itself is an ordinary file, so an action on IT never needed this.
  Affects: --archive
  Affected by: --archive-mount, --archive-write
  See also: [Archives](#topic-archive), [--archive-mount](#flag-archive-mount), [--archive-write](#flag-archive-write), [--archive](#flag-archive)

<a id="flag-archive-mount"></a>

- `--archive-mount` - let -exec / -ok run on an archive member by mounting its container read-only _(global, xff)_
  The alternative to `--archive-extract`, answering the same question - what path can a child process open for a member? - with the container itself instead of a copy. The container is mounted read-only (once, whichever members are visited), so `{}` names a path INSIDE the archive: a tool that only reads it (a compiler, a checksum, `grep`) sees the real member and nothing is written anywhere. That also removes extraction's trap, where an in-place formatter edits a temporary copy and reports success while the archive keeps its old content: here the mount has no write path at all, so such a tool fails honestly. Mounting is a per-MACHINE capability (it needs the fuse extra AND a runtime FUSE library and permission to mount), so where it cannot happen the run says so once and falls back to extraction rather than failing - which is why this flag is safe to keep in a config file. `--archive-extract` is still what arms writing through a copy; this one arms reading in place.
  Affects: --archive, --archive-extract
  See also: [Archives](#topic-archive), [--archive](#flag-archive), [--archive-extract](#flag-archive-extract)

<a id="flag-archive-write"></a>

- `--archive-write, -Z[-|+|++]` - arm both archive write flags (--archive-extract + --archive-delete) _(global, xff)_
  One spelling for "let actions touch members", because the two write flags are almost always wanted together: `--archive-extract` so `-exec` / `-ok` can run over a member, and `--archive-delete` so `-delete` can remove one. It is exactly those two flags and nothing else - the dive MODE is untouched. The short form is the UPPER-case archive ladder: `-Z` is `-z` with writing armed, `-Z+` is `-z+` with it, `-Z++` is `-z++` with it. Case carries the capability and the signs carry the level, so a slipped shift key changes which of the two you asked for, never both - and arming is not doing, since an action still has to ask for the write and `--safe` / `--dry-run` still apply. The level and the arming resolve as separate axes with later winning, so `-Z++ -z-` keeps writing armed with diving off; `-Z-` is the full reset, disarming writing and turning diving off together.
  Affects: --archive-delete, --archive-extract
  See also: [Archives](#topic-archive), [--archive-delete](#flag-archive-delete), [--archive-extract](#flag-archive-extract)

<a id="flag-archive-any"></a>

- `--archive-any` - under --archive=all, offer EVERY file to the reader, not only likely names _(global, xff)_
  By default `all` only opens a file the walk met whose NAME looks like a container (`.tar`, `.tgz`, `.zip`, `.jar`, `.phar`, ... - the reader's formats plus the packages that are one of them underneath). Without that gate, walking a source tree would open and format-bid every `.cc` and every binary in it, so the cost of diving would fall on runs that dive nothing. The name is only a heuristic, and this flag is the way out of it: an archive called `blob` or `backup.dat` is found with --archive-any and missed without. It costs a read of every candidate file, which is why it is not the default. A file NAMED on the command line is always opened - pointing xff at it is the request - so this flag changes nothing for `--archive=roots`.
  Affects: --archive
  See also: [Archives](#topic-archive), [--archive](#flag-archive)

<a id="flag-archive-separator"></a>

- `--archive-separator=STRING` - string between container and member in a member path (default `!`) _(global, xff)_
  A member path is `<container><separator><member>`, and there is no single ecosystem convention - `!` (JAR / Java URLs), `#` (fragment style), and the multi-character `!/` or `#/` other tools print all exist - so this is a presentation choice rather than something hard-coded. ANY string is accepted, not a fixed menu, so xff can emit what another system accepts. Rendering is plain concatenation and xff adds or removes no slash, so a member stored with a leading slash keeps it: `a.tgz!/rooted` (and with `--archive-separator=!/`, the doubled `a.tgz!//rooted`, which is why plain `!` is the better default). Parsing splits at the FIRST occurrence and takes the remainder verbatim, so a path xff printed round-trips. A plain `/` is allowed and composes with globs, but is lossy - a real directory named x.tar becomes indistinguishable from an archive - so it is never the default.
  See also: [Archives](#topic-archive)

<a id="flag-archive-prefix"></a>

- `--archive-prefix=[URI|STRING]` - prefix a member path: empty (default), URI, or any literal string _(global, xff)_
  One of:

  - `(empty)` - no prefix - a bare path, `a.tgz!inner/x` (the default)
  - `URI` - the receiving ecosystem's own URL: `phar://`, `jar:file:...!/`, else `archive:`
  - `STRING` - any other value is used literally, e.g. `--archive-prefix=vfs:`

  Empty (the default) prints the bare path, `a.tgz!inner/x`. `URI` renders a URL the RECEIVING tool will accept, which means the ecosystem's own where one owns the format: a `.phar` as PHP's `phar:///abs/a.phar/inner/x`, a `.jar` / `.war` / `.ear` as Java's `jar:file:/abs/a.jar!/pkg/C.class`, and everything else as `archive:///abs/a.tar!x` for an absolute container or the opaque `archive:a.tgz!x` for a relative one - `archive://a.tgz` would be WRONG, since `//` starts the authority and would make `a.tgz` a host name. The choice is by EXTENSION, because that is what the claim is: a jar IS a zip, and only its name says which readers expect it. Those two spellings fix the separator as well, so `--archive-separator` does not reach them; the BARE path keeps one separator whatever the container is, since being re-pasteable matters more there than matching a foreign form. Any other value is used LITERALLY (e.g. `--archive-prefix=vfs:`), the same freedom `--archive-separator` has; `URI` is the one keyword, spelled in caps like `RE2` / `PCRE2` / `GLOB`. There is deliberately no `none` value: it would be indistinguishable from a literal prefix spelled `none`, which is why empty means no prefix. Applies to PARSING too - under a prefix, a bare path is not accepted as a member path, so the spellings never silently interchange.
  See also: [Archives](#topic-archive)

### Concurrency and ordering

<a id="flag-jobs"></a>

- `-j N, -j=N, --jobs=N|all` - directory-read and concurrent -exec workers (all = every detected core) _(global, xff)_
  `N` is a positive integer; use `-j 4`, `-j=4`, or `--jobs=4`. The conventional attached short form `-j4` is also accepted. Every form accepts `all` in place of `N`. `-j 1` makes directory reads and `-exec ... ;` synchronous. At larger values, xff reads directories on `N` worker threads and may independently keep up to `N` semicolon-form `-exec` / `-execdir` children outstanding. Reads and children can overlap; `N` is not one shared operation budget. The children's truth value is therefore success on launch. The `... +` batch forms still run once after the walk and propagate a failing exit status. With no flag, xff uses one fewer than the detected cores, capped at 15 and floored at 1; find and rg modes use every detected core. `all` always means every detected core.
  See also: [Output](#topic-output), [Statistics](#topic-stats)

<a id="flag-sort"></a>

- `--sort[=<ORDER>]` - sibling/traversal ordering (default depends on the mode) _(global, xff)_
  ORDER is one of:

  - `none` - filesystem order, whatever the directory yields (fastest)
  - `dir` - sort each directory's entries (a bare --sort; also spelled name)
  - `subtree` - sorted entries with each subtree inlined contiguously
  - `tree` - path-ordered depth-first traversal within each root
  - `roots` - sort root operands; retain filesystem order below them
  - `global` - sort the roots, then walk each in tree order
  - `score` - best `-fuzzy` match first (buffers everything; needs `-fuzzy`)

  Traversal order and result buffering are separate. Every mode materializes one directory listing, and `--jobs` may read additional directory listings ahead; none of the path-order modes buffers every match. `none` preserves each directory's filesystem order. `dir` sorts a directory's complete child listing, emits that listing, then descends into its sorted children. `subtree` emits sorted non-directory children first, then each sorted directory or container subtree contiguously. `tree` visits each sorted child and its descendants before the next child, giving lexicographic depth-first order WITHIN each root while retaining command-line root order. `roots` stable-sorts only the root operands and otherwise behaves like `none`. `global` stable-sorts the roots and applies `tree` below each one, producing a total hierarchical order by root and then path. Duplicate or overlapping roots remain separate walks and can therefore repeat paths. `-depth` makes every mode post-order (children before their parent); `none` retains filesystem sibling order; `roots` does too, while every other ordered mode uses sorted sibling order. The default is per style: xff sorts per directory, while find and rg leave the order unspecified. `score` is the odd one out: the others are TRAVERSAL orders the walk streams, while a `-fuzzy` score only exists once an entry has been evaluated, so results are buffered and ranked after the walk (best first, ties keeping the walk's own order). It needs `-fuzzy` or `-ifuzzy` in the expression - ranking by a value nothing produced is a mistake, not an empty ordering - and side-effecting actions such as `-exec` still run during the walk, so only the printed listing is reordered.
  Affected by: --pack
  See also: [Output](#topic-output), [Statistics](#topic-stats), [--pack](#flag-pack)

### Matching

<a id="flag-block-size"></a>

- `--block-size=SIZE` - bytes per bare/-b block for -size and -blocks (default 512) _(global, xff)_
  A bare number is bytes. Explicit `B`/`kB`/`MB`/... units use SI powers of 1000; `KiB`/`MiB`/... use IEC powers of 1024. Legacy `k`/`M`/`G`/... remain binary for find compatibility. The value must be positive and fit in 64 bits. Lowercase `b` is invalid here because defining a block in blocks is circular. This changes the comparison unit for both `-size` (apparent bytes) and `-blocks` (allocated bytes); it does not change filesystem metadata or the fixed units printed by `-ls`.
  See also: [Size units](#topic-size), [Output](#topic-output)

<a id="flag-exact"></a>

- `--exact` - disable xff's filesystem-native case folding for name/path/fuzzy matching _(global, xff)_
  In xff mode, the otherwise case-sensitive `-name`, `-path`, `-fuzzy`, and `-fuzzypath` matchers follow the containing volume: xff folds ASCII case on a case-insensitive volume and compares exactly on a case-sensitive one. `--exact` opts out and makes those matchers byte-case-exact unless `--case=insensitive` or an explicitly insensitive primary (`-iname`, `-ipath`, `-ifuzzy`, `-ifuzzypath`) requests folding. Find mode is already exact by default. The volume probe is cached per device and safely defaults to exact matching if unavailable.
  See also: [Regex matching](#topic-regex), [Regex grammars](#topic-grammars)

<a id="flag-case"></a>

- `--case=<MODE>, -i, -s[-|+]` - letter case for matchers: -i insensitive, -s/-s+ smart, -s- sensitive (rg -> smart) _(global, xff)_
  MODE is one of:

  - `sensitive` - match exactly (-s-)
  - `insensitive` - fold case (-i)
  - `smart` - fold case unless the pattern contains ASCII uppercase (-s / -s+)

  Controls the otherwise case-sensitive name, path, symlink-target, fuzzy, regex, and content matchers (`-name`, `-path`, `-lname`, `-fuzzy`, `-fuzzypath`, `-regex`, `-rxc`, `-grep`). Their `-i...` variants always fold independently. `sensitive` matches exactly; `insensitive` (`-i`) folds case; `smart` (`-s` / `-s+`) folds only when the pattern is all free of ASCII uppercase letters and matches exactly otherwise; `-s-` forces `sensitive`. For name, path, and fuzzy matching, xff's filesystem-native folding can additionally apply unless `--exact` is present. rg defaults to `smart`; xff and find default to `sensitive`.
  See also: [Regex matching](#topic-regex), [Regex grammars](#topic-grammars)

<a id="flag-regextype"></a>

- `--regextype=<GRAMMAR>` - match engine: RE2, ERE, EXACT, FNMATCH, GLOB, SHGLOB, or PCRE2 (a build extra) _(global, xff)_
  GRAMMAR is one of:

  - `ERE` - platform POSIX extended regular expressions via regcomp(3)
  - `EXACT` - a literal string; metacharacters are plain text
  - `FNMATCH` - flat shell wildcard; `*` matches any character including `/`
  - `GLOB` - path-aware shell glob; wildcards and classes are component-local
  - `PCRE2` - Perl syntax (lookaround, backreferences); a build extra
  - `RE2` - linear-time regular expressions (the default)
  - `SHGLOB` - GLOB plus `{a,b}` brace alternation, so `*.{cc,h}` matches either

  Selects one grammar for every `-regex`/`-iregex`, `-rxc`/`-irxc`, and `-grep` pattern in the run; the last occurrence wins. `RE2` is the default. Selecting `PCRE2` in a build without that extra is a hard error; see `--help=extras` for availability.
  Affects: -regex, -iregex, -rxc, -irxc, -grep, -capture, -capturedir
  Affected by: --re2, --pcre
  See also: [Regex matching](#topic-regex), [Regex grammars](#topic-grammars), [--re2](#flag-re2), [--pcre](#flag-pcre), [-regex](#primary-regex), [-iregex](#primary-iregex), [-rxc](#primary-rxc), [-irxc](#primary-irxc), [-grep](#primary-grep), [-capture](#primary-capture), [-capturedir](#primary-capturedir)

<a id="flag-re2"></a>

- `--re2` - select the fast, linear-time RE2 grammar _(global, xff)_
  A convenient command-line spelling of `--regextype=RE2`. It overrides a grammar selected by configuration; among grammar selectors, the last occurrence wins.
  Affects: --regextype, -regex, -iregex, -rxc, -irxc, -grep, -capture, -capturedir
  See also: [Regex matching](#topic-regex), [Regex grammars](#topic-grammars), [--regextype](#flag-regextype), [-regex](#primary-regex), [-iregex](#primary-iregex), [-rxc](#primary-rxc), [-irxc](#primary-irxc), [-grep](#primary-grep), [-capture](#primary-capture), [-capturedir](#primary-capturedir)

<a id="flag-pcre"></a>

- `--pcre` - select the PCRE2 grammar (a build extra) _(global, xff)_
  A convenient command-line spelling of `--regextype=PCRE2`. It overrides a grammar selected by configuration; among grammar selectors, the last occurrence wins. PCRE2 is available only in a full build, and selecting it in a lean build is a usage error.
  Affects: --regextype, -regex, -iregex, -rxc, -irxc, -grep, -capture, -capturedir
  See also: [Regex matching](#topic-regex), [Regex grammars](#topic-grammars), [--regextype](#flag-regextype), [-regex](#primary-regex), [-iregex](#primary-iregex), [-rxc](#primary-rxc), [-irxc](#primary-irxc), [-grep](#primary-grep), [-capture](#primary-capture), [-capturedir](#primary-capturedir)

### Path filters

<a id="flag-exclude"></a>

- `--exclude=GLOB` - skip paths matching a gitignore-style glob (repeatable; a matched directory is pruned) _(global, xff)_
  See also: [Ignore and VCS traversal](#topic-ignore)

<a id="flag-include"></a>

- `--include=GLOB` - re-include paths a --exclude would skip, matching a gitignore-style glob (repeatable) _(global, xff)_
  See also: [Ignore and VCS traversal](#topic-ignore)

### Classification databases

<a id="flag-lang-db"></a>

- `--lang-db=FILE` - overlay language metadata and suffix/filename mappings from JSON; repeatable _(global, xff)_
  Loads a JSON object keyed by canonical language name. Each value may set `type`, `color`, `group`, and `source`, plus string arrays `aliases`, `extensions`, and `filenames`. Later files override earlier files and compiled data. Extensions may include their leading dot and may contain multiple parts; matching folds suffix case while exact filenames retain case. Conflicts between two languages in ONE file follow `--lang-conflicts`.
  Affects: -lang
  Affected by: --lang-conflicts
  See also: [Content](#topic-content), [--lang-conflicts](#flag-lang-conflicts), [-lang](#primary-lang)

<a id="flag-lang-conflicts"></a>

- `--lang-conflicts=error|first|last` - resolve ambiguous suffix or filename claims within one language vocabulary file _(global, xff)_
  One of:

  - `error` - reject two languages claiming one suffix or filename in the same file (default)
  - `first` - keep the first claim in that file
  - `last` - keep the last claim in that file

  Controls only ambiguity inside one `--lang-db` file. Layering remains deterministic: a later file intentionally overrides earlier files and compiled data. `error` is the default; `first` or `last` is an explicit compatibility escape hatch for imported databases.
  Affects: --lang-db
  See also: [Content](#topic-content), [--lang-db](#flag-lang-db)

<a id="flag-mime-vocabulary"></a>

- `--mime-vocabulary=FILE` - overlay media-type metadata and extension mappings from JSON; repeatable _(global, xff)_
  Loads a JSON object keyed by canonical media type. Each value may set `description`, `source`, `charset`, boolean `compressible`, and string arrays `aliases` and `extensions`. Later files override earlier files and compiled data. An extension may include its leading dot; matching folds case. Conflicts between two types in ONE file follow `--mime-conflicts`.
  Affects: -mime
  Affected by: --mime-conflicts
  See also: [Content](#topic-content), [--mime-conflicts](#flag-mime-conflicts), [-mime](#primary-mime)

<a id="flag-mime-conflicts"></a>

- `--mime-conflicts=error|first|last` - resolve ambiguous extension claims within one MIME vocabulary file _(global, xff)_
  One of:

  - `error` - reject two media types claiming one extension in the same file (default)
  - `first` - keep the first claim in that file
  - `last` - keep the last claim in that file

  Controls only ambiguity inside one `--mime-vocabulary` file. Layering remains deterministic: a later file intentionally overrides earlier files and compiled data. `error` is the default; `first` or `last` provides an explicit compatibility escape hatch for imported databases.
  Affects: --mime-vocabulary
  See also: [Content](#topic-content), [--mime-vocabulary](#flag-mime-vocabulary)

### Filter & Ignore

<a id="flag-gitignore"></a>

- `--gitignore[=off|auto|on], -g[-|+]` - respect .gitignore files: -g = auto (only in a git repo), -g+/=on always, -g-/=off never _(global, xff)_
  One of:

  - `off` - ignore .gitignore files entirely (also `-g-`, no / false / 0)
  - `auto` - respect .gitignore only inside a git working tree (a bare `-g` / `--gitignore`)
  - `on` - respect it anywhere, git repository or not (also `-g+`, yes / true / 1)

  Reads .gitignore rules while walking, including nested .gitignore files, .git/info/exclude, and core.excludesFile. `-g` / `auto` activates only inside a git working tree; `-g+` / `=on` forces it anywhere; `-g-` / `=off` disables it. Independent of `--ignore-files` (.ignore / .xffignore).
  See also: [Ignore and VCS traversal](#topic-ignore)

<a id="flag-ignore-files"></a>

- `--ignore-files` - respect per-directory .ignore and .xffignore files (off by default) _(global, xff)_
  See also: [Ignore and VCS traversal](#topic-ignore)

<a id="flag-ignore-file"></a>

- `--ignore-file=PATH` - read an extra gitignore-format file, rooted at its own directory (repeatable) _(global, xff)_
  See also: [Ignore and VCS traversal](#topic-ignore)

<a id="flag-no-ignore"></a>

- `--no-ignore, -u` - disable all ignore-file processing (.gitignore/.ignore/.xffignore) _(global, xff)_
  See also: [Ignore and VCS traversal](#topic-ignore)

<a id="flag-ignore-vcs"></a>

- `--ignore-vcs` - respect version-control ignore files (.gitignore / .git/info/exclude / core.excludesFile) _(global, xff)_
  The rg-style affirmative for the VCS ignore-file layer - today git's (.gitignore at any depth, .git/info/exclude, core.excludesFile), the same layer -g / --gitignore auto enables. Use it to countermand an earlier --no-ignore-vcs or a style default. Independent of --ignore-files (.ignore / .xffignore), which keep their own switch; --no-ignore / -u still turns off every ignore source. Last of the ignore-mode flags wins.
  See also: [Ignore and VCS traversal](#topic-ignore)

<a id="flag-no-ignore-vcs"></a>

- `--no-ignore-vcs` - do not respect version-control ignore files (keeps .ignore / .xffignore) _(global, xff)_
  Drops the VCS ignore-file layer (git's .gitignore / .git/info/exclude / core.excludesFile) while leaving --ignore-files (.ignore / .xffignore) untouched - that is the difference from --no-ignore / -u, which turns off every ignore source. Today git is the only VCS ignore file xff reads, so this is nearly --gitignore=off. Last of the ignore-mode flags wins.
  See also: [Ignore and VCS traversal](#topic-ignore)

<a id="flag-hidden"></a>

- `--hidden` - include hidden dotfiles in the walk (default: find/xff show, rg skips) _(global, xff)_
  See also: [Ignore and VCS traversal](#topic-ignore)

<a id="flag-no-hidden"></a>

- `--no-hidden` - skip hidden dotfiles (the rg default; opts find/xff out) _(global, xff)_
  See also: [Ignore and VCS traversal](#topic-ignore)

<a id="flag-skip-vcs"></a>

- `--skip-vcs[=<LIST>]` - prune VCS metadata dirs (.git, .hg, ...); bare/=all = every known VCS, =LIST a subset _(global, xff)_
  LIST is one of:

  - `git` - .git
  - `hg` - .hg
  - `svn` - .svn
  - `jj` - .jj
  - `bzr` - .bzr
  - `darcs` - _darcs
  - `cvs` - CVS
  - `all` - every known VCS (the bare-flag default)
  - `none` - off (same as --no-skip-vcs)

  Prunes version-control metadata directories at any depth (like ripgrep / fd), so a search never wades into repo plumbing. Bare `--skip-vcs` (or `=all`) covers every known VCS: `git` (.git), `hg` (.hg), `svn` (.svn), `jj` (.jj), `bzr` (.bzr), `darcs` (_darcs), `cvs` (CVS). A comma list (`--skip-vcs=git,hg`) is an explicit, frozen subset - it never changes if a VCS is added to the default set later. `--no-skip-vcs` (or `=none`) turns it off. Independent of `--hidden`, so the user's own dotfiles (.bazelrc, .gitignore) still show. `-g` / gitignore mode implies `--skip-vcs=git` (only .git); an explicit `--skip-vcs` overrides that. Default off otherwise.
  See also: [Ignore and VCS traversal](#topic-ignore)

<a id="flag-no-skip-vcs"></a>

- `--no-skip-vcs` - keep VCS metadata dirs in the walk (opts out of --skip-vcs and the -g .git default) _(global, xff)_
  See also: [Ignore and VCS traversal](#topic-ignore)

### Result formatting

<a id="flag-format"></a>

- `--format=<FORMAT>` - output format: plain, nul, jsonl, csv, tsv, aligned, markdown (md), tree; default plain _(global, xff)_
  FORMAT is one of:

  - `plain` - one path per line (the default)
  - `nul` - NUL-separated paths (for xargs -0)
  - `jsonl` - one JSON object per match
  - `csv` - comma-separated columns
  - `tsv` - tab-separated columns
  - `aligned` - column-aligned table
  - `markdown` - a Markdown table (also `md`)
  - `tree` - an indented directory tree

  `markdown` (alias `md`) also renders ordinary, comparison-result, and paired summary tables as Markdown, with vertically aligned separators and right-aligned numeric headers and values in both source and rendered tables. Summary schemas come from `--summary`; `--columns` selects listing fields and cannot change summary columns. Markdown summaries have descriptive headings above their scope and table. `--no-header` omits those headings, table headers, and Markdown separator rows when producing fragments. Expression actions retain their own output formats.
  See also: [Output](#topic-output)

<a id="flag-no-header"></a>

- `--no-header` - omit the header row from tabular --format (csv/tsv/aligned/markdown; on by default) _(global, xff)_
  See also: [Output](#topic-output)

<a id="flag-columns"></a>

- `--columns=FIELD,...` - columns for tabular --format, from the {field} vocabulary (e.g. path,size,mtime) _(global, xff)_
  See also: [Output](#topic-output)

### Tree comparison and diffs

<a id="flag-compare"></a>

- `--compare[=status|diff|summary]` - compare two roots as selected statuses, a unified diff, or a summary _(global, xff)_
  One of:

  - `summary` - shorthand for `--compare=status --compare-select=none --summary=compare`
  - `status` - one tab-separated selected result kind and relative path per record
  - `diff` - a unified tree diff suitable for saving as a patch

  Requires exactly two roots. `--compare=summary` expands at its position to `--compare=status --compare-select=none --summary=compare`; later flags may override its settings. Bare `--compare` and `--compare=status` emit only discrepancies as tab-separated `left-only`, `right-only`, or `different` records. `--compare=diff` emits one unified tree diff, suitable for redirecting to a patch file; `--diff-context` and `--diff-algorithm` tune it. Unlike `-diff TARGET`, which is an expression action comparing each match from one walk with a templated target and therefore cannot discover target-only paths, `--compare` walks both roots independently and pairs the matches by relative path. The ordinary expression, ignore, hidden-file, archive, traversal, and `-P` / `-H` / `-L` symlink rules apply unchanged to each side; comparison itself enables none of them. Regular files are compared byte for byte (text and binary). Unfollowed symlinks are compared by target. Both walks complete before comparison records are emitted in bytewise relative-path order; `--sort` affects each walk, not that final order. In status mode, `--path-encoding=escape` makes control bytes in the relative path unambiguous. `--summary` / `--summary=compare` append results and combined bytes by entry type and status. Results count each pair once; bytes include both sides, including two copies of identical files. Directories and special entries are included; directory equality compares the entry kind, not its subtree. Ordinary summary groupings default to `--summary-scope=compare`: one table with left and right columns across all comparison categories. Explicit `--summary-scope=all` combines both sides; `--summary-scope=root` separates roots. `--compare` changes this default regardless of option order.
  Affected by: --compare-select, --diff-algorithm, --diff-context, --summary-precision
  See also: [Comparing trees](#topic-compare), [--compare-select](#flag-compare-select), [--diff-algorithm](#flag-diff-algorithm), [--diff-context](#flag-diff-context), [--summary-precision](#flag-summary-precision)

<a id="flag-compare-select"></a>

- `--compare-select=KIND,...` - tree-comparison results to emit: left-only, right-only, identical, different, all, or none _(global, xff)_
  Selects comma-separated result kinds for `--compare`. The default is `left-only,right-only,different`, so equal files stay silent. `all` selects every kind. `none` or an empty value suppresses per-path output; comparison summaries still count all results. `identical` is available with status output and is rejected with `--compare=diff`, where an unchanged file has no patch representation.
  Affects: --compare
  See also: [Comparing trees](#topic-compare), [--compare](#flag-compare)

<a id="flag-diff-algorithm"></a>

- `--diff-algorithm=naive|direct|myers` - diff engine for -diff and tree diffs: naive, direct, or myers (the default) _(global, xff)_
  One of:

  - `myers` - minimal diff, as git computes it (the default)
  - `direct` - line-by-line, no alignment search
  - `naive` - the simple longest-common-subsequence walk

  Affects: -diff, --compare
  See also: [Comparing trees](#topic-compare), [-diff](#primary-diff), [--compare](#flag-compare)

<a id="flag-diff-ignore"></a>

- `--diff-ignore=TOKEN,...` - normalize -diff comparison: ws, change, trail, blank, case, eofnl (comma-separated) _(global, xff)_
  Sets the normalization used by `-diff`; the last value wins. It may be saved in user config or an explicit `--xffrc=FILE`, and a command-line value overrides the configured value. An empty value disables configured normalization. Tokens are `ws`, `change`, `trail`, `blank`, `case`, and `eofnl`, comma-separated.
  Affects: -diff
  See also: [Comparing trees](#topic-compare), [Content](#topic-content), [-diff](#primary-diff)

<a id="flag-diff-ignore-matching"></a>

- `--diff-ignore-matching=REGEX` - -diff ignores lines matching this regex (RE2) _(global, xff)_
  Drops matching lines before `-diff` compares the two inputs. It may be saved in user config or an explicit `--xffrc=FILE`; the last value wins, so a command-line value overrides configuration. An empty value disables a configured expression. The expression uses RE2.
  Affects: -diff
  See also: [Comparing trees](#topic-compare), [Content](#topic-content), [-diff](#primary-diff)

<a id="flag-diff-format"></a>

- `--diff-format=u|c|n|y` - default -diff format: u/unified (default), c/context, n/normal, y/side-by-side _(global, xff)_
  One of:

  - `u` - unified, the diff -u shape (the default; also spelled unified)
  - `c` - context, the diff -c shape (also context)
  - `n` - normal, the plain diff shape (also normal)
  - `y` - side by side, the diff -y shape (also side-by-side)

  Affects: -diff
  See also: [Comparing trees](#topic-compare), [Content](#topic-content), [-diff](#primary-diff)

<a id="flag-diff-context"></a>

- `--diff-context=N` - default -diff context lines (3); overrides --context for -diff, and -diff:uN overrides it _(global, xff)_
  Affects: -diff, --compare
  Affected by: --context
  See also: [Comparing trees](#topic-compare), [--context](#flag-context), [-diff](#primary-diff), [--compare](#flag-compare)

### Output values and actions

<a id="flag-hash-algorithm"></a>

- `--hash-algorithm=<ALGO>` - default digest for -hash / {hash} (sha256 default; md5, sha512, blake3, and more) _(global, xff)_
  ALGO is one of:

  - `blake2b` - BLAKE2b, 512-bit
  - `blake2b_256` - BLAKE2b, 256-bit
  - `blake3` - BLAKE3 (fast, parallel)
  - `md5` - 128-bit legacy (fast, collision-broken)
  - `sha1` - 160-bit legacy (collision-broken)
  - `sha224` - SHA-2, 224-bit
  - `sha256` - SHA-2, 256-bit (the default)
  - `sha384` - SHA-2, 384-bit
  - `sha3_224` - SHA-3 (Keccak), 224-bit
  - `sha3_256` - SHA-3 (Keccak), 256-bit
  - `sha3_384` - SHA-3 (Keccak), 384-bit
  - `sha3_512` - SHA-3 (Keccak), 512-bit
  - `sha512` - SHA-2, 512-bit
  - `sha512_224` - SHA-2, 512/224 truncated
  - `sha512_256` - SHA-2, 512/256 truncated

  Sets the default digest algorithm for the `-hash` action and the `{hash}` field. `sha256` is the default; a `-hash:ALGO` spec or a `{hash:ALGO}` qualifier overrides it per use.
  See also: [Fields](#topic-fields), [Output](#topic-output)

<a id="flag-hash-encoding"></a>

- `--hash-encoding=hex|base64` - default -hash / {hash} rendering: hex (default) or base64 _(global, xff)_
  One of:

  - `hex` - lower-case hex digits, as the sha256sum family prints (the default)
  - `base64` - standard padded base64 (RFC 4648), the Subresource-Integrity spelling

  See also: [Fields](#topic-fields), [Output](#topic-output)

<a id="flag-path-encoding"></a>

- `--path-encoding=raw|escape` - plain-output path byte encoding: raw (verbatim, default) or escape (C-escape controls) _(global, xff)_
  One of:

  - `raw` - the path's bytes verbatim, as find writes them (the default)
  - `escape` - C-escape control bytes, so a newline in a name cannot forge a line

  See also: [Output](#topic-output)

<a id="flag-template"></a>

- `--template=TEMPLATE` - render each match through a field template ({path}, {name}, ...) _(global, xff)_
  See also: [Output](#topic-output), [Fields](#topic-fields)

<a id="flag-implicit-print"></a>

- `--implicit-print=yes|no` - force the default -print on or off _(global, xff)_
  One of:

  - `yes` - print every match even when the expression has its own action (also on / true / 1)
  - `no` - never add the default print (also off / false / 0)

  See also: [Output](#topic-output)

### Archive creation

<a id="flag-pack"></a>

- `--pack=FILE` - write every match into a new archive at FILE instead of listing them _(global, xff)_
  The counterpart of `--archive`: instead of reading a container the walk BUILDS one, so the member list comes from the whole expression vocabulary rather than from a shell pipeline into `tar`. The output NAME picks the format - `--help=archive` lists exactly what this binary writes, from the writer's own table rather than a copy kept here, and the single-word shortcuts (`.tgz`, `.txz`, `.tbz2`, `.tzst`, `.tlz`, `.taZ`) mean what they do everywhere else; a name carrying no format is a usage error reported BEFORE the walk, since finding out afterwards would waste the traversal. Each member is stored under the entry's path relative to the search root it was found under, in the order the walk produced it - so `--sort` decides the order inside the archive, and nothing is renamed or re-rooted behind your back. Like `--summary` it is a sink: it replaces the per-match listing, while explicit actions still run, so add `-print` to watch what goes in. The archive is written after the walk and renamed into place only when complete, so an interrupted run leaves no half archive and an existing FILE survives a failed one. A file the walk meets that IS the output is skipped rather than packed into itself. An archive MEMBER cannot be packed: reading files out of one container to re-pack them into another is its own feature, and until it exists the run is refused rather than quietly short. A build-time extra, like `--archive`.
  Affects: --sort
  Affected by: --pack-option, --pack-level
  See also: [Archives](#topic-archive), [--pack-option](#flag-pack-option), [--pack-level](#flag-pack-level), [--sort](#flag-sort)

<a id="flag-pack-option"></a>

- `--pack-option=NAME=VALUE|@FILE.json` - tune how `--pack` writes: repeatable, last value for a NAME wins _(global, xff)_
  The general knob behind `--pack-level`. NAME is XFF's own vocabulary, not the archive library's: each name is translated to whatever the linked writer calls the same thing, so an unknown name is a usage error rather than a silent no-op, the accepted set is listed by `--help=archive` straight from the writer's table, and swapping or upgrading that library changes a translation table instead of the flags you type. A name that exists but does not apply to the chosen output format is refused too, naming the formats it does apply to - `zip64` is a zip idea, `threads` is not a gzip one. Everything is checked before the walk starts, so a typo costs no traversal and writes no file. `--pack-option=@FILE.json` reads one JSON object whose keys are option names and whose values are strings, integers, or booleans; booleans become `yes` or `no`. File and inline forms may be repeated and are expanded in command-line order, so the last value for a name wins across both forms.
  Affects: --pack
  See also: [Archives](#topic-archive), [--pack](#flag-pack)

<a id="flag-pack-level"></a>

- `--pack-level=N` - compression level for `--pack` (gzip/xz/lzip/lzma/zip 0-9, bzip2/lz4 1-9, zstd 1-22) _(global, xff)_
  How hard the compressor works, on the scale the chosen format uses; left alone it is the format's own default. Exactly `--pack-option=level=N`, kept as its own spelling because it is the common knob for compressors that expose a level - the same relationship `-Z` has to `--archive-write`. On a plain `.tar` it is a usage error rather than a no-op, because there is no compressor to set a level on and a silently ignored level reads as a smaller archive that never arrives. Legacy Unix `compress` has no level knob, so `.tar.Z` refuses this option too.
  Affects: --pack
  See also: [Archives](#topic-archive), [--pack](#flag-pack)

### Statistics

<a id="flag-summary-scope"></a>

- `--summary-scope=SCOPE,...` - summarize all roots together, each root, or selected comparison categories _(global, xff)_
  One of:

  - `all` - combine all input roots
  - `root` - separate input roots
  - `left-only` - comparison entries present only on the left
  - `right-only` - comparison entries present only on the right
  - `different` - different comparison pairs, with each side separate
  - `identical` - identical comparison pairs, with each side separate
  - `left` - alias for `left-only`
  - `right` - alias for `right-only`
  - `diff` - `left-only,right-only,different`
  - `compare` - `left-only,right-only,different,identical`

  Selects populations for each ordinary `--summary` grouping. `all` combines every root; `root` separates input roots. Without an explicit scope, the default is `all` outside comparison and `compare` when `--compare` is active, regardless of option order. An explicit scope overrides that conditional default; repeated scopes use the last occurrence. Comparison categories require `--compare`: `left` aliases `left-only`, `right` aliases `right-only`, `diff` expands to `left-only,right-only,different`, and `compare` expands to `left-only,right-only,different,identical`. Aliases expand in order and duplicate scopes are removed. Repeating the flag replaces the preceding list. Selection is independent of `--compare-select`. Selected categories share one table per grouping, with left and right count, count percentage, size, and size percentage columns. Each group appears once, combining entries from the selected categories. Percentages use each side's full selected category population before `--top`; missing entries display a dash. `all` counts both copies. Requires an active file summary such as `--summary` or `--summary=ext`; otherwise it is an error. `--compare=summary` expands to `--summary=compare`, which only counts comparison results and does not satisfy this requirement.
  Affects: --summary
  See also: [Statistics](#topic-stats), [Comparing trees](#topic-compare), [--summary](#flag-summary)

<a id="flag-summary"></a>

- `--summary[=<GROUP>]` - count, count percentage, size, and size percentage table; repeatable _(global, xff)_
  GROUP is one of:

  - `none` - clear all previously requested summaries
  - `compare` - comparison counts, combined sizes, and percentages by type and status; requires `--compare`
  - `overall` - one row aggregated over all matches
  - `type` - by file type
  - `ext` - by extension
  - `lang` - by programming language
  - `mime` - by media (MIME) type
  - `user` - by owner
  - `group` - by owning group
  - `hash` - by file digest (dedup: identical files share a bucket; reads every file)
  - `hash-verification` - verified / failed tally from exactly one reached `-hasheq`
  - `{template}` - by any field value, e.g. `--summary='{ext}-{type}'`

  With `--compare`, bare `--summary` or `--summary=compare` appends result counts, combined left-plus-right sizes, and percentages by type and status, plus a total after the comparison output. With explicit `--summary-scope`, bare `--summary` also adds ordinary count and size statistics. Other groupings default to one left/right table with `--compare` (`--summary-scope=compare`), and combine all roots otherwise (`--summary-scope=all`). Explicit scope selection overrides this conditional default regardless of option order. `--summary-scope` selects combined, per-root, or comparison-category tables. Outside comparison, replaces the per-match listing with an aggregate table: match count and total size per group (overall, by type, extension, programming language, media (MIME) type, user (owner), owning group, file digest, or hash-verification result). The categorical keys reuse the {mime}/{user}/{group}/{hash} field vocabulary; --summary=hash groups identical files into one bucket (a dedup count, reading every file). `--summary=hash-verification` requires exactly one `-hasheq` and counts its `verified` or `failed` verdict even when that verdict makes the complete expression false; an entry that short-circuits before reaching `-hasheq` is not counted. Empty expected values and unreadable entries are failed, matching `-hasheq` itself. A {template} key groups by any field value (e.g. --summary='{ext}-{type}'); a single m// extraction key (--summary='{capture.NAME:m/re/\1/}') groups per extracted line, so a per-file command's multi-line output tallies per key (e.g. git-blame lines per author) - the size column is not meaningful there. Repeatable: each --summary is its own table (e.g. --summary=ext --summary=type), printed in order. Percentages use the complete table totals before `--top`; a zero denominator yields zero percent. Sizes sum entry metadata sizes, including directory metadata, never recursive subtree sizes. `--format=markdown` (or `md`) renders these tables with grouping-specific headings above their scopes and fixed summary columns. Each accounting note stays with its table, separated from the next summary by a blank line. Exact bytes align with scaled sizes at the decimal boundary. `--format=jsonl` includes `count_percent` and `size_percent` alongside `count` and `bytes`. --top=N limits the rows of each, --summary-precision sets the scaled-size digits, and --format=jsonl emits one object per group for scripts.
  Affected by: --summary-scope, --top, --summary-precision
  See also: [Statistics](#topic-stats), [--summary-scope](#flag-summary-scope), [--top](#flag-top), [--summary-precision](#flag-summary-precision)

<a id="flag-histogram"></a>

- `--histogram=BUCKET[:MEASURE]` - bar chart per bucket: a count or sum/mean/min/max of size|lines (repeatable) _(global, xff)_
  A terminal reduction like --summary, drawn as bars. BUCKET groups the matches - a category (overall, type, ext, lang, mime, user (owner), or group) or a numeric-range field (size / lines by order of magnitude, depth per level, drawn as an ascending distribution). The optional :MEASURE is the bar's value - `count` (the default) or an aggregate `sum(FIELD)` / `mean(FIELD)` / `min(FIELD)` / `max(FIELD)` over a numeric FIELD (size or lines). A numeric metric needs an aggregator (`ext:lines` is an error; `ext:sum(lines)` is not). Repeatable and combinable with --summary - both are fed by one walk and replace the per-match listing. Bars scale to the tallest, use Unicode block characters on a UTF-8 locale (see --unicode) or ASCII '#' otherwise; --top=N keeps the N tallest and --format=jsonl emits one object per bar for scripts.
  Affected by: --top, --histogram-width
  See also: [Statistics](#topic-stats), [--top](#flag-top), [--histogram-width](#flag-histogram-width)

### Sharded files

<a id="flag-shards"></a>

- `--shards[=auto|SCHEME,...]` - collapse each set of sharded files (e.g. data-00000-of-00010) to one line _(global, xff)_
  One of:

  - `auto` - recognize every built-in scheme (the default when bare `--shards`)
  - `of` - only `<stem>-<index>-of-<total>` (TFRecord-style)
  - `dotnum` - only `<stem>.<NNN>` (7-Zip-style volumes)
  - `underscore` - only `<stem>_<NNN>`

  Recognizes sharded-file naming conventions and collapses each logical set to a single line instead of listing every shard. Bare `--shards` (or `=auto`) enables all built-in schemes: `<stem>-<index>-of-<total>` (`of`), `<stem>.<NNN>` (`dotnum`), and `<stem>_<NNN>` (`underscore`). Restrict to specific schemes with a comma list, e.g. `--shards=of,dotnum`. Grouping is per-directory; files that match no scheme are listed unchanged. Off by default.
  See also: [Statistics](#topic-stats)

<a id="flag-shards-show"></a>

- `--shards-show=first|wildcard|count` - how a collapsed shard set's line reads (default first) _(global, xff)_
  One of:

  - `first` - the representative (lowest-index) shard's path (the default)
  - `wildcard` - the masked-index name, e.g. `arc.???` (or `f-` idx `-of-003`)
  - `count` - the wildcard name plus the shard count, e.g. `arc.??? (3 shards)`

  Picks each collapsed set's display: `first` = the representative (lowest-index) shard's path; `wildcard` = the masked-index name (the index digits shown as `???`); `count` = the `wildcard` name plus the shard count. An incomplete set is always annotated `(present/expected - INCOMPLETE)`. Only meaningful with `--shards`.
  See also: [Statistics](#topic-stats)

<a id="flag-shards-dedup"></a>

- `--shards-dedup=first|mtime|error` - how same-index shard duplicates are resolved (default first) _(global, xff)_
  One of:

  - `first` - keep the lexicographically-first name among same-index copies (the default)
  - `mtime` - keep the newest by modification time (ties break on name)
  - `error` - treat a same-index duplicate as an error (non-zero exit)

  When two files are the same logical shard (they differ only by an opaque tail, e.g. a regeneration id), `--shards-dedup` picks which is the representative: `first` keeps the lexicographically-first name; `mtime` keeps the newest; `error` treats the duplicate as an error and fails the run (non-zero exit). Also selects the representative used when `-shard-status` classifies physical files.
  See also: [Statistics](#topic-stats)

<a id="flag-shard-pattern"></a>

- `--shard-pattern=REGEX` - a custom shard scheme via a named-capture regex (repeatable); the escape hatch _(global, xff)_
  Defines a custom sharded-file scheme for `--shards` and `-shard-status` when the built-ins do not fit. REGEX is an RE2 pattern with named groups: `(?P<stem>...)` and `(?P<index>...)` are required, `(?P<total>...)` and `(?P<dup>...)` are optional. Repeatable; the patterns are tried in order, before the built-in schemes.
  See also: [Statistics](#topic-stats)

### Content-match output

<a id="flag-count"></a>

- `--count, -c` - with -grep, print a per-file matching-line count (path:count) instead of the lines _(global, xff)_
  Affects: -grep
  See also: [Content](#topic-content), [-grep](#primary-grep)

<a id="flag-context"></a>

- `--context=SPEC` - -grep context lines: N both sides, or A:N,B:N,C:N for after/before/both _(global, xff)_
  `--context=2` is grep's `-C 2` (two lines either side); the A / B / C keys inside the value select one side (`--context=A:3,B:1`), which is what `--after-context` and `--before-context` spell one at a time. xff has NO single-dash `-A` / `-B` / `-C`: those letters are unclaimed for now (see TODO.md), and a single-dash flag would be an expression primary under xff's dash-count rule rather than a whole-run option.
  Affects: -grep, -diff, --diff-context
  See also: [Content](#topic-content), [-grep](#primary-grep), [-diff](#primary-diff), [--diff-context](#flag-diff-context)

<a id="flag-after-context"></a>

- `--after-context=N` - with -grep, print N lines of context after each match (= --context=A:N) _(global, xff)_
  Affects: -grep
  See also: [Content](#topic-content), [Regex matching](#topic-regex), [-grep](#primary-grep)

<a id="flag-before-context"></a>

- `--before-context=N` - with -grep, print N lines of context before each match (= --context=B:N) _(global, xff)_
  Affects: -grep
  See also: [Content](#topic-content), [Regex matching](#topic-regex), [-grep](#primary-grep)

### Result limits

<a id="flag-max-results"></a>

- `--max-results=N` - list at most N matched entries without stopping or truncating reductions _(global, xff)_
  Caps the implicit result listing after the whole expression, across every branch and every per-instance `-first` / `-top` filter. It does NOT stop traversal: `--summary`, `--histogram`, `--count`, and archive packing still see the complete matched set rather than silently reporting a partial walk. Explicit expression actions (`-print`, `-grep`, `-exec`, and friends) keep their own positional semantics and are not suppressed; use `-first` or `-top` before an action to cap the entries that reach it. With one capped filter this flag is usually redundant; its distinct use is an aggregate ceiling such as `\( -type f -first 10 \) -o \( -type d -first 5 \) --max-results=12`. Last occurrence wins. A malformed or negative count is a usage error; `0` lists none.
  See also: [Output](#topic-output), [Safety](#topic-safety)

<a id="flag-top"></a>

- `--top=N` - with --summary or --histogram, keep only the N largest/tallest groups _(global, xff)_
  Affects: --summary, --histogram
  See also: [Statistics](#topic-stats), [--summary](#flag-summary), [--histogram](#flag-histogram)

### Statistics display

<a id="flag-histogram-width"></a>

- `--histogram-width=N` - cell width the tallest --histogram bar fills (default 40) _(global, xff)_
  Affects: --histogram
  See also: [Statistics](#topic-stats), [--histogram](#flag-histogram)

<a id="flag-summary-precision"></a>

- `--summary-precision=N` - fraction digits for comparison percentages and human-readable summary sizes (default 2) _(global, xff)_
  Affects: --summary, --compare
  See also: [Statistics](#topic-stats), [--summary](#flag-summary), [--compare](#flag-compare)

### Terminal display

<a id="flag-color"></a>

- `--color[=auto|always|never]` - colorize the plain listing by file type and language: auto (a tty), always, or never _(global, xff)_
  One of:

  - `auto` - colour only when stdout is a terminal (the default; a bare --color is always)
  - `always` - colour even through a pipe or pager (also on / yes / true / 1)
  - `never` - no colour at all (also off / no / false / 0)

  Colorizes the plain listing by file type and, when the active language vocabulary supplies a colour, programming language. auto colorizes only when stdout is a terminal and NO_COLOR is unset; always forces color even through a pipe or pager and deliberately overrides NO_COLOR; never disables it.
  Affected by: --color-scheme
  See also: [Output](#topic-output), [Environment](#topic-environment), [--color-scheme](#flag-color-scheme)

<a id="flag-color-scheme"></a>

- `--color-scheme=<SCHEME>` - which palette colour comes from: the terminal's ls theme, or xff's own _(global, xff)_
  SCHEME is one of:

  - `auto` - ls OR xff: the theme when $LS_COLORS / $LSCOLORS is set, else xff's scheme (the default; also spelled `ls+xff`, `ls-or-xff` or `default`)
  - `ls` - the theme alone ($LS_COLORS, else $LSCOLORS): what it omits prints plain, as in ls
  - `merged` - the theme where it speaks, xff's type/language colour where it does not (also `ls-and-xff`)
  - `xff` - xff's built-in type and language scheme, ignoring $LS_COLORS

  Colour is a whole-run choice, so this one palette is used by every surface that colours - the plain listing and -ls alike; they cannot disagree. $LS_COLORS is the variable `ls` and `dircolors` use, and xff reads the same keys: the two-letter types (`di`, `ln`, `ex`, `pi`, `so`, `bd`, `cd`, `fi`) and the per-extension `*.tar=` entries. Where only BSD's $LSCOLORS is set - the macOS case - that is read instead: its 11 letter pairs carry the same types in a fixed order, with no way to say "leave this plain" and no per-extension entries, so `merged` is the interesting scheme there. $LS_COLORS wins when both are set, being the richer format. Both variables are read on every platform rather than one per OS: which one is SET is better evidence than which system this is (a macOS shell with GNU coreutils is themed through $LS_COLORS, and $LSCOLORS is not macOS-only), and the fixed 22-character shape makes the BSD one self-validating. "Use ls's colours" turns out to mean three different things, so each has its own name, spelled the way logic spells it: `+` is OR, and the merge is AND. `auto` (the default, also `ls+xff` or `ls-or-xff`) is the theme OR xff's scheme - a theme that is set at all is the whole answer, and with none set xff's scheme is, so the decision is per VARIABLE; `default` is a fourth spelling of it, for a config file that wants whatever the default currently is. `ls` is the theme ALONE, so a type it never mentions prints uncoloured exactly as in a real ls listing (and with no theme set, nothing is coloured). `merged` (also `ls-and-xff`) is the theme AND xff's scheme, merged per KEY: the theme where it speaks, xff's colour for every key it omits - for a sparse theme you want filled in. (`ls&xff` is deliberately not accepted: an unquoted `&` backgrounds the command.) `xff` ignores $LS_COLORS entirely. In xff's scheme a regular non-executable file uses the active language vocabulary's `#RRGGBB` colour when present. A theme's matching extension or `fi` value wins; `ls` and a themed `auto` do not add language colours, while `merged` uses one only for a key the theme omitted. Executables and non-regular files retain their type colours. An EMPTY value in the theme (`di=` or `fi=`) is it saying "leave these plain" and is honoured as such; a malformed entry is skipped rather than failing the run, as in ls. Whether colour is emitted at all is --color's business, not this flag's.
  Affects: --color
  See also: [Output](#topic-output), [Environment](#topic-environment), [--color](#flag-color)

<a id="flag-unicode"></a>

- `--unicode[=auto|always|never]` - --format=tree connectors: auto (a UTF-8 locale), always (Unicode), or never (ASCII) _(global, xff)_
  One of:

  - `auto` - Unicode connectors when the locale is UTF-8, else ASCII (the default)
  - `always` - force the Unicode connectors (also on / yes / true / 1)
  - `never` - force the ASCII connectors (also off / no / false / 0)

  Selects the box-drawing characters --format=tree connects nodes with. auto uses Unicode when the locale (LC_ALL / LC_CTYPE / LANG) is UTF-8, else ASCII; always forces the Unicode connectors; never forces the ASCII ones.
  See also: [Output](#topic-output), [Environment](#topic-environment)

<a id="flag-human"></a>

- `--human[=si|iec|off]` - size units for -ls / --summary: si (kB/MB, default), iec (KiB/MiB), off (bytes); xff -> si _(global, xff)_
  One of:

  - `si` - powers of 1000: kB, MB, GB (the default; also 1000, --si, a bare --human)
  - `iec` - powers of 1024: KiB, MiB, GiB (also 1024)
  - `off` - plain byte counts, no unit suffix

  See also: [Output](#topic-output), [Environment](#topic-environment)

<a id="flag-si"></a>

- `--si` - human sizes in SI (kB/MB, 1000^N); an alias for --human=si (the --human default) _(global, xff)_
  See also: [Output](#topic-output), [Environment](#topic-environment)

<a id="flag-buffer"></a>

- `--buffer[=auto|off|all|N[kMGT]|NMB|NMiB]` - buffer to size columns (-ls / tables): auto, off, all, N[kMGT] rows, or NMB/NMiB bytes _(global, xff)_
  Row windows use a bare count or decimal `k`/`M`/`G`/`T` multiplier. Byte budgets require an explicit trailing `B`: `B`/`kB`/`MB`/.../`EB` are SI, while `KiB`/`MiB`/.../`EiB` are IEC. The distinct suffixes keep rows and bytes unambiguous.
  See also: [Output](#topic-output), [Environment](#topic-environment)

<a id="flag-width"></a>

- `--width[=auto|none|COLS]` - wrap column for plain --help text: auto (terminal width, else unwrapped), none, or a count _(global, xff, command-line-only)_
  Command-line only; rejected in configuration files.
  Wraps the flowing text of --help and --help=TOPIC (option and topic descriptions) to a column width. auto uses the terminal width when stdout is a terminal (honoring $COLUMNS), and leaves output unwrapped when it is not (a pipe or file); none (or 0) disables wrapping; a positive integer sets a fixed width. Aligned vocabulary tables and example blocks keep their own layout. Does not affect the file listing, `--man`, or formatted full help.
  See also: [Output](#topic-output), [Environment](#topic-environment)

<a id="flag-pager"></a>

- `--pager[=help|auto|always|never|COMMAND]` - page output: help only, auto (all on a tty), always, never, or an explicit command _(global, xff)_
  One of:

  - `help` - page help, man, and Markdown on a terminal (the default)
  - `auto` - page every pageable output on a terminal
  - `always` - page every pageable output, even through a pipe
  - `never` - never page (same as `--no-pager`)
  - `COMMAND` - always page through this explicit shell command

  Pages every pageable output: long meta output (`--help`, `--help=TOPIC`, `--man`) and the file listing, including action rows such as `-ls`. The default `help` pages only those meta surfaces and only on a terminal. `auto` adds every pageable listing when stdout is a terminal; `always` also pages through a pipe; `never` (or `--no-pager`) disables it. Automatic command selection prefers an installed `less -FRX`, then `more`, and only then consults `$XFF_PAGER` followed by ambient `$PAGER`. `--pager=COMMAND` always uses COMMAND directly, so `--pager="$PAGER"` is the explicit way to request the process environment's choice. Listings stream through one pager for the whole walk, so the first screen appears while the walk is still running and quitting ends the run quietly. Paging steps aside for an expression that needs the terminal itself (`-ok`, `-okdir`, `-exec`, `-execdir`, which can hand the terminal to an editor) and for `--quiet`, which prints nothing to page; those runs are simply unpaged.
  See also: [Output](#topic-output), [Environment](#topic-environment)

<a id="flag-no-pager"></a>

- `--no-pager` - never page any output (an alias for --pager=never) _(global, xff)_
  See also: [Output](#topic-output), [Environment](#topic-environment)

### Exit code control

<a id="flag-quiet"></a>

- `--quiet, -q` - suppress output; exit 0 if anything matched, else 1 (-q: grep-compatible) _(global, xff)_
  See also: [Output](#topic-output)

<a id="flag-exit-match"></a>

- `--exit-match` - keep output; exit 0 if anything matched, else 1 _(global, xff)_
  See also: [Output](#topic-output)

### Safety

<a id="flag-block-policy-categories"></a>

- `--block-policy-categories=LIST` - select categories with dedicated blocking controls (config only) _(global, xff, config-only)_
  One of:

  - `archive` - use dedicated controls for archive output and member edits
  - `temp` - use dedicated controls within the declared temporary root
  - `output` - use dedicated controls within the declared output root

  Selects separate controls; this directive itself neither blocks nor allows operations. A comma-separated category list; empty means ordinary file controls for all categories (default). `archive`, `temp`, and `output` are supported. Allowed once before all sections in system or user configuration. Each file chooses its own policy, including for its named sections. Neither named sections, explicit `.xffrc` files, nor the CLI may set it. See `--help=safety` for the operation table and archive replacement tradeoff.
  See also: [Safety](#topic-safety)

<a id="flag-temp-root"></a>

- `--temp-root=PATH` - declare an existing absolute temp root (config only) _(global, xff, config-only)_
  Allowed once in unsectioned system or user INI; a system declaration wins. Permissions cover descendants recursively; the root itself remains protected. Roots and descendant traversal must not contain symlinks. See `--help=safety`.
  See also: [Safety](#topic-safety)

<a id="flag-output-root"></a>

- `--output-root=PATH` - declare an existing absolute output root (config only) _(global, xff, config-only)_
  Allowed once in unsectioned system or user INI; a system declaration wins. Permissions cover descendants recursively; the root itself remains protected. Roots and descendant traversal must not contain symlinks. See `--help=safety`.
  See also: [Safety](#topic-safety)

<a id="flag-block-directory-creation"></a>

- `--block-directory-creation` - unconditionally prohibit directory creation; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-directory-creation"></a>

- `--safe-block-directory-creation` - include directory creation in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-directory-creation"></a>

- `--no-safe-block-directory-creation` - exclude directory creation from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-directory-deletion"></a>

- `--block-directory-deletion` - unconditionally prohibit directory deletion; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-directory-deletion"></a>

- `--safe-block-directory-deletion` - include directory deletion in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-directory-deletion"></a>

- `--no-safe-block-directory-deletion` - exclude directory deletion from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-temp-file-writing"></a>

- `--block-temp-file-writing` - unconditionally prohibit temp file writing; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-temp-file-writing"></a>

- `--safe-block-temp-file-writing` - include temp file writing in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-temp-file-writing"></a>

- `--no-safe-block-temp-file-writing` - exclude temp file writing from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-temp-file-overwrite"></a>

- `--block-temp-file-overwrite` - unconditionally prohibit temp file overwrite; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-temp-file-overwrite"></a>

- `--safe-block-temp-file-overwrite` - include temp file overwrite in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-temp-file-overwrite"></a>

- `--no-safe-block-temp-file-overwrite` - exclude temp file overwrite from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-temp-file-deletion"></a>

- `--block-temp-file-deletion` - unconditionally prohibit temp file deletion; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-temp-file-deletion"></a>

- `--safe-block-temp-file-deletion` - include temp file deletion in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-temp-file-deletion"></a>

- `--no-safe-block-temp-file-deletion` - exclude temp file deletion from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-temp-directory-creation"></a>

- `--block-temp-directory-creation` - unconditionally prohibit temp directory creation; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-temp-directory-creation"></a>

- `--safe-block-temp-directory-creation` - include temp directory creation in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-temp-directory-creation"></a>

- `--no-safe-block-temp-directory-creation` - exclude temp directory creation from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-temp-directory-deletion"></a>

- `--block-temp-directory-deletion` - unconditionally prohibit temp directory deletion; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-temp-directory-deletion"></a>

- `--safe-block-temp-directory-deletion` - include temp directory deletion in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-temp-directory-deletion"></a>

- `--no-safe-block-temp-directory-deletion` - exclude temp directory deletion from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-output-file-writing"></a>

- `--block-output-file-writing` - unconditionally prohibit output file writing; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-output-file-writing"></a>

- `--safe-block-output-file-writing` - include output file writing in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-output-file-writing"></a>

- `--no-safe-block-output-file-writing` - exclude output file writing from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-output-file-overwrite"></a>

- `--block-output-file-overwrite` - unconditionally prohibit output file overwrite; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-output-file-overwrite"></a>

- `--safe-block-output-file-overwrite` - include output file overwrite in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-output-file-overwrite"></a>

- `--no-safe-block-output-file-overwrite` - exclude output file overwrite from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-output-file-deletion"></a>

- `--block-output-file-deletion` - unconditionally prohibit output file deletion; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-output-file-deletion"></a>

- `--safe-block-output-file-deletion` - include output file deletion in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-output-file-deletion"></a>

- `--no-safe-block-output-file-deletion` - exclude output file deletion from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-output-directory-creation"></a>

- `--block-output-directory-creation` - unconditionally prohibit output directory creation; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-output-directory-creation"></a>

- `--safe-block-output-directory-creation` - include output directory creation in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-output-directory-creation"></a>

- `--no-safe-block-output-directory-creation` - exclude output directory creation from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-output-directory-deletion"></a>

- `--block-output-directory-deletion` - unconditionally prohibit output directory deletion; later settings cannot clear it _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-output-directory-deletion"></a>

- `--safe-block-output-directory-deletion` - include output directory deletion in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-output-directory-deletion"></a>

- `--no-safe-block-output-directory-deletion` - exclude output directory deletion from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for directory scope, capability composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-file-deletion"></a>

- `--block-file-deletion` - unconditionally prohibit deletion; later flags cannot remove this block _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-file-deletion"></a>

- `--safe-block-file-deletion` - include deletion in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-file-deletion"></a>

- `--no-safe-block-file-deletion` - exclude deletion from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-execution"></a>

- `--block-execution` - unconditionally prohibit execution; later flags cannot remove this block _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-execution"></a>

- `--safe-block-execution` - include execution in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-execution"></a>

- `--no-safe-block-execution` - exclude execution from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-file-writing"></a>

- `--block-file-writing` - unconditionally prohibit writing; later flags cannot remove this block _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-file-writing"></a>

- `--safe-block-file-writing` - include writing in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-file-writing"></a>

- `--no-safe-block-file-writing` - exclude writing from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-file-overwrite"></a>

- `--block-file-overwrite` - unconditionally prohibit overwrite; later flags cannot remove this block _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-file-overwrite"></a>

- `--safe-block-file-overwrite` - include overwrite in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-file-overwrite"></a>

- `--no-safe-block-file-overwrite` - exclude overwrite from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-archive-writing"></a>

- `--block-archive-writing` - unconditionally prohibit archive writing; later flags cannot remove this block _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-archive-writing"></a>

- `--safe-block-archive-writing` - include archive writing in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-archive-writing"></a>

- `--no-safe-block-archive-writing` - exclude archive writing from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-archive-overwrite"></a>

- `--block-archive-overwrite` - unconditionally prohibit archive overwrite; later flags cannot remove this block _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-archive-overwrite"></a>

- `--safe-block-archive-overwrite` - include archive overwrite in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-archive-overwrite"></a>

- `--no-safe-block-archive-overwrite` - exclude archive overwrite from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-block-archive-content-writing"></a>

- `--block-archive-content-writing` - unconditionally block archive content writing _(global, xff)_
  Applies to member edits of existing archives under `--block-policy-categories=archive`. See `--help=safety` for the operation table and whole-archive replacement tradeoff.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-archive-content-writing"></a>

- `--safe-block-archive-content-writing` - include in the safe profile: archive content writing _(global, xff)_
  Applies to member edits of existing archives under `--block-policy-categories=archive`. See `--help=safety` for the operation table and whole-archive replacement tradeoff.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-archive-content-writing"></a>

- `--no-safe-block-archive-content-writing` - exclude from the safe profile: archive content writing _(global, xff)_
  Applies to member edits of existing archives under `--block-policy-categories=archive`. See `--help=safety` for the operation table and whole-archive replacement tradeoff.
  See also: [Safety](#topic-safety)

<a id="flag-block-archive-content-overwrite"></a>

- `--block-archive-content-overwrite` - unconditionally block archive content overwrite _(global, xff)_
  Applies to member edits of existing archives under `--block-policy-categories=archive`. See `--help=safety` for the operation table and whole-archive replacement tradeoff.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-archive-content-overwrite"></a>

- `--safe-block-archive-content-overwrite` - include in the safe profile: archive content overwrite _(global, xff)_
  Applies to member edits of existing archives under `--block-policy-categories=archive`. See `--help=safety` for the operation table and whole-archive replacement tradeoff.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-archive-content-overwrite"></a>

- `--no-safe-block-archive-content-overwrite` - exclude from the safe profile: archive content overwrite _(global, xff)_
  Applies to member edits of existing archives under `--block-policy-categories=archive`. See `--help=safety` for the operation table and whole-archive replacement tradeoff.
  See also: [Safety](#topic-safety)

<a id="flag-block-archive-content-deletion"></a>

- `--block-archive-content-deletion` - unconditionally prohibit archive deletion; later flags cannot remove this block _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-safe-block-archive-content-deletion"></a>

- `--safe-block-archive-content-deletion` - include archive deletion in the active safe-mode restrictions _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe-block-archive-content-deletion"></a>

- `--no-safe-block-archive-content-deletion` - exclude archive deletion from the safe profile; unconditional blocks still apply _(global, xff)_
  See `--help=safety` for capability coverage, profile composition, and dry-run limits.
  See also: [Safety](#topic-safety)

<a id="flag-no-safe"></a>

- `--no-safe` - disable the safe-mode profile; unconditional blocks remain enforced _(global, xff)_
  See also: [Safety](#topic-safety)

<a id="flag-safe"></a>

- `--safe` - activate the configured safe-mode profile _(global, xff)_
  Initially the profile blocks execution and file/archive deletion, writing, and overwrite. `--safe-block-*` and `--no-safe-block-*` customize it without activating it. `--no-safe` deactivates the profile. Unconditional `--block-*` restrictions always apply. Prohibited actions are rejected before traversal; overwrite collisions are enforced at creation.
  See also: [Safety](#topic-safety)

<a id="flag-dry-run"></a>

- `--dry-run` - preview permitted actions without writes, deletion, or execution _(global, xff)_
  Policy is checked first; dry run cannot bypass a block. Reads and normal output remain enabled. File output, deletion, and archives are previewed. Commands are reported but never launched. A skipped command or capture has no result: evaluation of that entry stops with an incomplete-preview error, rather than guessing which subsequent actions would run.
  See also: [Safety](#topic-safety)

<a id="flag-skip-unsupported"></a>

- `--skip-unsupported` - warn and skip a predicate a filesystem cannot evaluate, not fail _(global, xff)_
  Applies when a predicate is unsupported for an entry's filesystem, most commonly an archive member that cannot provide an operation available on the host filesystem. Without this flag the unsupported operation is a hard error; with it the entry is skipped and the reason is reported. Ordinary I/O and traversal errors remain errors.
  See also: [Safety](#topic-safety)

### Fields & Exec

<a id="flag-exec-fields"></a>

- `--exec-fields` - render -exec tokens through the field vocabulary ({name}, {path}, ...) _(global, xff)_
  See also: [Fields](#topic-fields), [Output](#topic-output)

<a id="flag-define"></a>

- `--define=NAME=VALUE` - define a value referenced as {def.NAME} _(global, xff)_
  See also: [Fields](#topic-fields), [Output](#topic-output)

### Time

<a id="flag-time-format"></a>

- `--time-format=FMT` - default format for time fields (a preset name or a strftime pattern) _(global, xff)_
  Sets the default rendering for time fields ({mtime}, {atime}, -printf %t, ...) when no per-field qualifier is given. Accepts a preset (iso, epoch, space, find) or any strftime pattern such as %Y-%m-%d. A per-field qualifier like {mtime:%H:%M} still overrides it.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="flag-timezone"></a>

- `--timezone=ZONE, --tz=ZONE` - zone for interpreting/formatting times (local, utc, an IANA name, or +HH:MM) _(global, xff)_
  The zone used to interpret and format every time. Accepts local, utc, an IANA name like Europe/London, or a fixed offset like +02:00. Affects time fields and -newerXt comparisons.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="flag-time-zone-suffix"></a>

- `--time-zone-suffix[=auto|always|never]` - show the zone offset on a time field: auto (per format), always, or never _(global, xff)_
  One of:

  - `auto` - each format's built-in default (the default)
  - `always` - force the offset, even on a format that omits it (also on / yes / true / 1)
  - `never` - drop the optional offset (also off / no / false / 0)

  Controls whether a time field's named preset renders its trailing zone (+0100, +01:00). `auto` keeps each preset's default (`space` / `iso` / `rfc3339` show it, `asctime` / `epoch` omit it); `never` drops it; `always` forces it, even on a preset that omits one. Accepts `true` / `yes` / `on` (= `always`) and `false` / `no` / `off` (= `never`). The inherently-zoned `zulu` / `zulu-dense` / `asn1z` always keep their mandatory Z, and a custom strftime `--time-format` is never altered - control its zone with %z / %Ez / %Z yourself. `asn1`'s zone is optional: `always` adds its ASN.1-style offset (+0100, no separator), `never` / `auto` leave it bare.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="topic-expressions"></a>

## Expression

### Tests

<a id="primary-name"></a>

- `-name ARG, -n ARG` - match the basename against a shell glob _(test, find)_
  Globs the entry's basename (last path component): `*` matches any run including none, `?` one character, `[...]` a class. Unlike the shell a leading dot is matched literally. Case follows `--case` - the xff default folds when the volume does (APFS / HFS+ / NTFS), while `--exact` or `--config=find` forces a byte-exact compare; `-iname` always folds. Contrast `-path` (whole path) and `-regex` (anchored pattern). Example: `xff . -name '*.log'`.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-iname"></a>

- `-iname ARG` - match the basename against a shell glob, case-insensitively _(test, find)_
  The always-case-insensitive `-name`: folds case regardless of `--case` or the volume.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-path"></a>

- `-path ARG, -p ARG` - match the whole path against a shell glob _(test, find)_
  Globs the whole path as printed (from the start point down), not just the basename. Unlike the shell, `*` and `?` DO match `/`, so `-path '*/build/*'` matches a build directory at any depth. Wildcards and case handling are `-name`'s. GNU spells this `-wholename`.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-ipath"></a>

- `-ipath ARG` - match the whole path against a shell glob, case-insensitively _(test, find)_
  The always-case-insensitive `-path` (whole-path glob).
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-wholename"></a>

- `-wholename ARG` - GNU synonym for -path _(test, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-iwholename"></a>

- `-iwholename ARG` - GNU synonym for -ipath _(test, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-lname"></a>

- `-lname ARG` - match the symlink target against a shell glob _(test, find)_
  Globs the symlink's target text - the path the link points AT, never the resolved destination - so a link matches even when its target is missing. Only a symbolic link can match, and with the default `-P` (or `-H`) a symlink is seen as itself. Wildcards and case handling are `-name`'s; `-ilname` always folds.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-ilname"></a>

- `-ilname ARG` - match the symlink target against a shell glob, case-insensitively _(test, find)_
  The always-case-insensitive `-lname` (symlink-target glob).
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-regex"></a>

- `-regex ARG` - match the whole path against a regular expression _(test, find)_
  Matches when the pattern matches the WHOLE path (anchored both ends, like find), not just a substring - use `.*` to match anywhere. Dialect is chosen by `-regextype` (RE2 by default); capture groups become `{1}`..`{N}` for a following `-exec` / `-printf`. Example: `xff . -regex '.*/[0-9]+\.log'`.
  Affected by: -E, --regextype, --re2, --pcre
  See also: [Regex matching](#topic-regex), [Regex grammars](#topic-grammars), [-E](#flag-e), [--regextype](#flag-regextype), [--re2](#flag-re2), [--pcre](#flag-pcre)

<a id="primary-iregex"></a>

- `-iregex ARG` - match the whole path against a regular expression, case-insensitively _(test, find)_
  The case-insensitive `-regex`: same whole-path anchoring and capture-group binding, matching without regard to case.
  Affected by: -E, --regextype, --re2, --pcre
  See also: [Regex matching](#topic-regex), [Regex grammars](#topic-grammars), [-E](#flag-e), [--regextype](#flag-regextype), [--re2](#flag-re2), [--pcre](#flag-pcre)

<a id="primary-regextype"></a>

- `-regextype ARG` - select the regex dialect for the following -regex/-iregex _(test, find)_
  See also: [Regex matching](#topic-regex), [Regex grammars](#topic-grammars)

<a id="primary-content"></a>

- `-content ARG` - match a literal substring in the file's content (xff) _(test, xff)_
  Matches when the file contains SUBSTRING literally (no regex metacharacters - the literal pair sidesteps grep's flavor ambiguity). Reads the file, so it is expensive; a non-regular, unreadable, or binary file (a NUL byte in the first 8 KiB) never matches. `-icontent` folds ASCII case. Use `-rxc` for a pattern. This is an xff extension `--config=find` rejects.
  See also: [Content](#topic-content)

<a id="primary-icontent"></a>

- `-icontent ARG` - match a literal substring in the file's content, case-insensitively (xff) _(test, xff)_
  The case-insensitive `-content`: folds ASCII case on the literal substring search.
  See also: [Content](#topic-content)

<a id="primary-rxc"></a>

- `-rxc ARG` - match the file's content against a regular expression (xff) _(test, xff)_
  The regex counterpart of `-content`: matches when the selected regex pattern is found ANYWHERE in the content (unanchored, like grep - use `^` / `$` to anchor), not the whole-file anchoring `-regex` applies to the path. Same expensive read and non-regular / unreadable / binary skip; `-irxc` folds case. An xff extension `--config=find` rejects.
  Affected by: -E, --regextype, --re2, --pcre
  See also: [Content](#topic-content), [-E](#flag-e), [--regextype](#flag-regextype), [--re2](#flag-re2), [--pcre](#flag-pcre)

<a id="primary-irxc"></a>

- `-irxc ARG` - match the file's content against a regular expression, case-insensitively (xff) _(test, xff)_
  The case-insensitive `-rxc`: folds case on the content regex search.
  Affected by: -E, --regextype, --re2, --pcre
  See also: [Content](#topic-content), [-E](#flag-e), [--regextype](#flag-regextype), [--re2](#flag-re2), [--pcre](#flag-pcre)

<a id="primary-text"></a>

- `-text[:FLAVOR]` - match a regular text file; -text[=git|posix|windows|apple] picks the definition (xff) _(test, xff)_
  TRUE for a regular, readable file whose content is text. Bare `-text` (or `=git`) is the default heuristic: no NUL byte in the first 8000 bytes (git's buffer_is_binary, also grep/ripgrep), line-ending-agnostic. One leading UTF-8 BOM is transparent. The strict flavors forbid a NUL ANYWHERE after that BOM and pin the line ending, requiring a final terminator (an empty file is vacuously complete): `=posix` = LF only, ends with a newline; `=windows` = CRLF only; `=apple` = CR only. Reads the file (expensive). A directory, symlink, device or unreadable file is not text (nor binary), so it never matches - `! -text` is NOT `-binary`. An xff extension `--config=find` rejects.
  See also: [Content](#topic-content)

<a id="primary-binary"></a>

- `-binary` - match a regular file whose content is binary (a NUL in the first 8 KiB) (xff) _(test, xff)_
  TRUE for a regular, readable file whose content is binary - a NUL byte in the first 8 KiB. The precise complement of `-text` WITHIN regular files: a directory, symlink, device or unreadable file is neither, so `-binary` is not `! -text`. Reads the file (expensive). An xff extension `--config=find` rejects.
  See also: [Content](#topic-content), [Fields](#topic-fields)

<a id="primary-eofnl"></a>

- `-eofnl` - match a regular file whose content ends with a newline (LF), or is empty (xff) _(test, xff)_
  TRUE for a regular, readable file whose content ends with a newline / LF (or is empty - a zero-line file is complete). Tests ONLY the final terminator, the other axis from `-text`/-binary: compose `-text` `-eofnl` for a well-formed (POSIX-style) text file, or `-text` ! `-eofnl` for the common lint 'a text file missing its final newline'. A CRLF file ends with LF too, so it also matches `-eofnl`; `-eofcrlf` is the strict CRLF form. Reads the file (expensive). An xff extension `--config=find` rejects.
  See also: [Content](#topic-content), [Fields](#topic-fields)

<a id="primary-eofcr"></a>

- `-eofcr` - match a regular file whose content ends with a bare CR, or is empty (xff) _(test, xff)_
  TRUE for a regular, readable file whose content ends with a bare carriage return / CR (or is empty). The classic-Mac / `-text:apple` final terminator, and the CR analogue of `-eofnl`: compose `-text:apple` `-eofcr` for a well-formed CR-terminated file, or `-text:apple` ! `-eofcr` for the missing final CR. A CRLF file ends with LF (not a bare CR), so it does NOT match `-eofcr`. Reads the file (expensive). An xff extension `--config=find` rejects.
  See also: [Content](#topic-content)

<a id="primary-eofcrlf"></a>

- `-eofcrlf` - match a regular file whose content ends with CRLF, or is empty (xff) _(test, xff)_
  TRUE for a regular, readable file whose content ends with CRLF (or is empty). The Windows / `-text:windows` final terminator, and the CRLF analogue of `-eofnl`: compose `-text:windows` `-eofcrlf` for a well-formed CRLF-terminated file, or `-text:windows` ! `-eofcrlf` for the missing final CRLF. Stricter than `-eofnl` (which any LF-ending file, including CRLF, satisfies). Reads the file (expensive). An xff extension `--config=find` rejects.
  See also: [Content](#topic-content)

<a id="primary-first"></a>

- `-first ARG` - true for the first N entries this instance sees, false after (xff) _(test, xff)_
  Caps a result set as it streams: TRUE for the first N entries reaching it, FALSE from then on. The count is PER USE, not per run, so each `-first` keeps its own budget - `\( -type f -first 10 \) -o \( -type d -first 5 \)` yields ten files AND five directories, which no whole-run flag could express. Because a FALSE test removes the entry from everything downstream, `-first` genuinely narrows the result set (the summary sees only those N); use `-collect` before it when you want the full set summarised and only a few shown. Which N you get follows `--sort`, like any other order-dependent behaviour. An xff extension `--config=find` rejects. A count that cannot be read is a usage error rather than an empty result set - `-first nope` is a typo, and returning nothing would be indistinguishable from a tree with no matches; `-first 0` IS valid and means none. Example: `xff . -type f -first 20`.
  See also: [Statistics](#topic-stats), [Output](#topic-output)

<a id="primary-top"></a>

- `-top ARG` - true for the N best fuzzy matches reaching this instance (xff) _(test, xff)_
  Keeps exactly the N entries with the best normalized fuzzy score reaching THIS use, then resumes the expression for those survivors. It is a TEST, not an output limit: everything to its left has already happened, while tests and actions to its right run after the walk only for entries that make the cut. Thus `-collect -top 10 -ls --summary` collects every good match, lists the ten best, and summarises all of them; `-top 10 -collect --summary` collects and summarises only the ten and prints no implicit listing. Each `-top` instance owns an independent candidate set, retained until the post-walk decision so a rejected entry can still take an `-o` alternative to the right. A tie keeps traversal order, making the result deterministic under a deterministic `--sort`. A fuzzy matcher must precede the node on every path that reaches it. All contributing fuzzy matchers must use the same model and threshold: scores from different models or differently strict predicates do not describe one ordering. A bare fuzzy matcher has a `0%` threshold. `-fuzzy:fzf:80% foo -top 10` therefore means the ten best good matches. An xff extension `--config=find` rejects. A malformed or negative count is a usage error; `-top 0` is valid and keeps none.
  See also: [Statistics](#topic-stats), [Output](#topic-output)

<a id="primary-shard-status"></a>

- `-shard-status ARG` - match complete, incomplete, or superfluous physical shards (xff) _(test, xff)_
  Classifies the physical shard files reaching THIS primary after the traversal, then resumes the expression. `complete` matches representatives belonging to a set with every expected index; `incomplete` matches representatives in a set with a missing expected index; `superfluous` matches same-index duplicate copies and indices outside a declared total. A non-shard file matches none. Completeness is computed per directory and only from entries that reach this node, so a predicate to its left intentionally narrows the cohort being validated; put ordinary actions to its right when they should run only for the selected status. This is a physical-file diagnostic independent of `--shards`: without `--shards` every selected file is listed, while `--shards` still controls logical-set collapsing. Custom `--shard-pattern` definitions and any scheme restriction from `--shards=SCHEME,...` apply. Example: `xff data -type f -shard-status incomplete -print`.
  See also: [Statistics](#topic-stats)

<a id="primary-fuzzy"></a>

- `-fuzzy[:MODEL[:PCT%]|PCT%] PATTERN` - match the basename loosely, optionally requiring a normalized score (xff) _(test, xff)_
  TRUE when PATTERN matches the entry's basename under the selected MODEL. The forms are `-fuzzy PATTERN`, `-fuzzy:MODEL PATTERN`, `-fuzzy:PCT% PATTERN`, and `-fuzzy:MODEL:PCT% PATTERN`; the default model is `fzf`, and `edit` aliases `levenshtein`. The models answer different questions. `sequence` is a literal ordered subsequence: `tmh` finds `the_main_header.h`. `fzf` adds fzf EXTENDED-SEARCH expressions: spaces AND terms, `|` joins OR alternatives, `'` requests exact matching, `^` and `$` anchor, `!` excludes, and `\ ` embeds a literal space. Quote a query containing spaces for the shell, for example `-fuzzy:fzf '^core go$ | rb$ | py$'`. As in fzf, anchors ignore whitespace at the corresponding candidate edge. An OR applies only inside its adjacent group: in that example `^core` remains a required AND term while `go$`, `rb$`, and `py$` are alternatives. Prefix an exact term with `!'` to exclude that fuzzy subsequence; `!^foo` excludes a prefix and `!foo$` a suffix. Backslash only escapes the next query character, so shell quoting and fzf query escaping are separate layers. `levenshtein` is normalized edit similarity (insert, delete, and substitute cost one), while `shingles` is unique character-bigram Jaccard similarity. The first two reject a candidate that is not a subsequence/query match; the latter two score every candidate. Case follows `--case` like `-name` does; `-ifuzzy` always folds. PCT requires normalized quality from 0 through 100, and `{fuzzy}` renders it. Multiple fuzzy tests compose through the expression: AND keeps the weakest required score and OR the best successful alternative, independent of predicate order. Ranking requires every fuzzy test to use the same MODEL and threshold (a bare test means `0%`): different domains are valid filters, but do not define one unambiguous ordering. In `fzf`, the percentage is the best alignment relative to an exact self-match: characters at a word start, matched consecutively, and matched early score higher. Thus nearby candidates can all match while receiving different scores; inspect them with `--format=tsv --columns=fuzzy,path`, rank them with `--sort=score`, or keep the best N with `-top N`. Use `-name` for a glob and `-regex` for a pattern. An xff extension `--config=find` rejects. Example: `xff . -fuzzy rdme --columns=fuzzy,path`.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-fuzzypath"></a>

- `-fuzzypath[:MODEL[:PCT%]|PCT%] PATTERN` - match the whole path loosely, optionally requiring a normalized score (xff) _(test, xff)_
  `-fuzzy` for the whole PATH instead of the basename - the `-path` to its `-name`. It accepts the same `:MODEL[:PCT%]` syntax and scoring, so `-fuzzypath:sequence eng/wlk` finds `xff/engine/walk.cc`, which no basename match could. It matches far more than `-fuzzy` does (every path shares its directories), so it is most useful RANKED: `--sort=score` puts the best match first, and `{fuzzy}` renders the score. Case follows `--case`; `-ifuzzypath` always folds. An xff extension `--config=find` rejects. Example: `xff . -fuzzypath eng/wlk --sort=score`.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-ifuzzy"></a>

- `-ifuzzy[:MODEL[:PCT%]|PCT%] PATTERN` - match the basename loosely, case-insensitively (xff) _(test, xff)_
  The always-case-insensitive `-fuzzy`: accepts the same `:MODEL[:PCT%]` syntax and folds ASCII case regardless of `--case` or the volume.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-ifuzzypath"></a>

- `-ifuzzypath[:MODEL[:PCT%]|PCT%] PATTERN` - match the whole path loosely, case-insensitively (xff) _(test, xff)_
  The always-case-insensitive `-fuzzypath`: accepts the same `:MODEL[:PCT%]` syntax and folds ASCII case regardless of `--case` or the volume.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-cmp"></a>

- `-cmp ARG` - true when the file's content is byte-identical to TARGET (a field template) (xff) _(test, xff)_
  See also: [Comparing trees](#topic-compare), [Content](#topic-content)

<a id="primary-similar"></a>

- `-similar[:WIDTH[:PCT%]|PCT%] TARGET` - match text whose word-shingle Jaccard similarity to TARGET reaches a threshold (xff) _(test, xff)_
  Compares a regular text file with TARGET, which is a {field} template evaluated per entry, using Jaccard overlap of unique contiguous word shingles. Bare `-similar TARGET` uses five-word shingles and requires `80%`; qualify it as `-similar:PCT%`, `-similar:WIDTH`, or `-similar:WIDTH:PCT%` to override either default. Words are case-folded ASCII alphanumeric or UTF-8 byte runs; punctuation and whitespace separate them. A short non-empty file contributes one shingle containing all its words. Non-regular, unreadable, or binary files do not match. This v1 answers whether each file resembles one reference; whole-tree clustering is deferred.
  See also: [Content](#topic-content)

<a id="primary-hasheq"></a>

- `-hasheq[:ALGO[/ENCODING]] EXPECTED` - true when the digest equals EXPECTED (a field template); -hasheq:ALGO[/ENC] (xff) _(test, xff)_
  Computes the file's digest and is true when it equals EXPECTED - a {field} template evaluated per entry, so it can name a sidecar value like `{def.SUMS}` or a capture. `-hasheq:ALGO[/ENCODING]` picks the algorithm (sha256 default; also sha1/sha512/...) and encoding (hex default, or base64); the same grammar as `-hash` / {hash}. It is a strict equality test (hex folds case). `! -hasheq` selects files whose digest differs (drift / corruption). Reads the whole file, so it is expensive.
  See also: [Content](#topic-content), [Fields](#topic-fields)

<a id="primary-type"></a>

- `-type ARG` - match the file type (f, d, l, b, c, p, s) _(test, find)_
  Matches the entry's type by letter: `f`=regular file, `d`=directory, `l`=symlink, `b`/`c`=block / char device, `p`=FIFO, `s`=socket. A GNU-style comma list is any-of, so `-type f,l` matches regular files or symlinks. Under the default `-P` a symlink is type `l`; `-xtype` tests its target's type instead. Unknown type letters and empty list elements are usage errors.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-xtype"></a>

- `-xtype ARG` - match the file type of a symlink's target _(test, find)_
  Like `-type`, but for a symlink it tests the type of the link's TARGET (the link is followed). A broken symlink has no target, so it reports as a symlink and `-xtype l` matches it, matching GNU find under the default `-P`. On a non-symlink it is identical to `-type`. Accepts the same type letters and comma lists; unknown or empty values are usage errors.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-mime"></a>

- `-mime ARG` - match the media type by extension against a glob, e.g. -mime 'image/*' (xff) _(test, xff)_
  xff extension: matches the media (MIME) type derived from the filename extension against a shell glob, so `image/*` matches png/jpg/... and `text/plain` is exact. The lean binary has a curated common-type table; the removable `mime-db` build extra supplies thousands of types, and repeatable `--mime-vocabulary=FILE` JSON layers override mappings and metadata. This is fast name classification, not content sniffing. The same value is the `{mime}` field; `{mime-category}`, `{mime-description}`, `{mime-charset}`, `{mime-compressible}`, and `{mime-source}` expose its metadata. Matching is always case-insensitive (MIME names are case-insensitive per RFC 2045/6838), so `IMAGE/*` behaves like `image/*`; `--case` / -i / -s do not affect it. See `--help=content` for the overlay schema and conflict policy.
  Affected by: --mime-vocabulary
  See also: [Content](#topic-content), [Fields](#topic-fields), [--mime-vocabulary](#flag-mime-vocabulary)

<a id="primary-lang"></a>

- `-lang ARG` - match the language by extension/filename against a glob, e.g. -lang 'C*' (xff) _(test, xff)_
  xff extension: matches the programming language inferred from the extension/filename against a shell glob, so `C*` matches C / C++ / C#. The lean binary has a curated common table; the removable GitHub Linguist build extra supplies hundreds of canonical records, and repeatable `--lang-db=FILE` JSON layers override mappings and metadata. Exact filenames win over the longest matching suffix. The same canonical value is `{lang}`; `{lang-type}`, `{lang-color}`, `{lang-group}`, and `{lang-source}` expose metadata. A pattern may also match an alias (`cpp` matches canonical `C++`). Matching is always case-insensitive and unaffected by `--case` / -i / -s. This is fast name classification, not Linguist's content/shebang heuristic classifier.
  Affected by: --lang-db
  See also: [Content](#topic-content), [Fields](#topic-fields), [--lang-db](#flag-lang-db)

<a id="primary-size"></a>

- `-size ARG` - match apparent size with legacy, explicit SI (MB), or IEC (MiB) units _(test, find)_
  Compares the file's apparent size. A bare number counts 512-byte blocks (find default); a unit suffix sets the scale: find's `c`/`w`/`k`/`M`/`G`/`T`/`P`/`E` are retained as legacy binary units; explicit `B`/`kB`/`MB`/... are SI powers of 1000, and `KiB`/`MiB`/... are IEC powers of 1024. A leading + / - means greater / less than. The size is rounded up to whole units, so `-size +100M` means larger than `100 MiB`, while `-size +100MB` means larger than `100 MB`. See `--help=size` and `-blocks` for allocated space.
  See also: [Size units](#topic-size), [Fields](#topic-fields)

<a id="primary-blocks"></a>

- `-blocks ARG` - match the allocated size (st_blocks); xff's disk-occupancy counterpart to -size _(test, xff)_
  Uses the same `[+|-]N[unit]` grammar as `-size`, but compares allocated disk space rather than apparent length. See `--help=size` for legacy, SI, and IEC units.
  See also: [Size units](#topic-size), [Fields](#topic-fields)

<a id="primary-links"></a>

- `-links ARG` - match the hard-link count _(test, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-inum"></a>

- `-inum ARG` - match the inode number _(test, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-samefile"></a>

- `-samefile ARG` - match files that share an inode with FILE _(test, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-fstype"></a>

- `-fstype ARG` - match the filesystem type (statfs) _(test, find)_
  Matches when the filesystem holding the entry has the given type name (e.g. `apfs`, `ext2/ext3`, `tmpfs`, `nfs`). The recognized names are platform-specific - macOS / BSD report `f_fstypename` verbatim, Linux maps the statfs magic to a find-compatible name - so a portable expression usually cannot assume one name across OSes.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-uid"></a>

- `-uid ARG` - match the numeric owner id _(test, find)_
  Matches the owner's numeric user id. Like find's numeric tests it accepts `+N` (greater than), `-N` (less than), or a bare N (exact). Match by login name with `-user` instead.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-gid"></a>

- `-gid ARG` - match the numeric group id _(test, find)_
  The group counterpart of `-uid`: the numeric group id, with `+N` / `-N` / bare-N. Match by group name with `-group` instead.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-user"></a>

- `-user ARG` - match the owner by name _(test, find)_
  Matches the owner by login name, resolved through the passwd database. A name with no passwd entry never matches, but a bare numeric argument is taken as a uid, so `-user 0` behaves like `-uid 0`. Exact match only (no `+` / `-`).
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-group"></a>

- `-group ARG` - match the group by name _(test, find)_
  The group counterpart of `-user`: matches by group name (via the group database), falling back to a numeric gid. Exact match only.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-nouser"></a>

- `-nouser` - match when the owner uid has no passwd entry _(test, find)_
  Matches when the entry's owner uid has NO entry in the passwd database - an orphaned owner, e.g. from a deleted account or an archive unpacked with foreign ids. Takes no argument. See `-nogroup` for the group side.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-nogroup"></a>

- `-nogroup` - match when the group gid has no group entry _(test, find)_
  Matches when the entry's group gid has no entry in the group database (the group side of `-nouser`).
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-newer"></a>

- `-newer ARG` - match when mtime is newer than the reference file's mtime _(test, find)_
  Matches when the entry's mtime is strictly newer than reference FILE's mtime. FILE is stat'd following symlinks; a missing or unreadable reference makes it false. This is the base of the -newerXY family: `-newerXY FILE` compares the entry's X time against the reference's Y time, where each of X and Y is a=access, c=status-change, m=modification, or B=birth - so `-newerac` is the entry's atime vs the reference's ctime. `-anewer` / `-cnewer` are the classic aliases. When Y is `t` the operand is a TIME STRING, not a file (see `-newermt`). A birth time the filesystem never recorded makes an X=B test a hard error and a Y=B reference a silent no-match.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-anewer"></a>

- `-anewer ARG` - match when atime is newer than the reference file's mtime (== -neweram) _(test, find)_
  find's classic spelling of `-neweram`: the entry's access time is newer than the reference file's modification time. See `-newer` for the -newerXY family.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-cnewer"></a>

- `-cnewer ARG` - match when ctime is newer than the reference file's mtime (== -newercm) _(test, find)_
  find's classic spelling of `-newercm`: the entry's status-change time is newer than the reference file's modification time. See `-newer` for the -newerXY family.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-neweraa"></a>

- `-neweraa ARG` - match when atime is newer than the reference file's atime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerac"></a>

- `-newerac ARG` - match when atime is newer than the reference file's ctime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-neweram"></a>

- `-neweram ARG` - match when atime is newer than the reference file's mtime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerca"></a>

- `-newerca ARG` - match when ctime is newer than the reference file's atime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newercc"></a>

- `-newercc ARG` - match when ctime is newer than the reference file's ctime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newercm"></a>

- `-newercm ARG` - match when ctime is newer than the reference file's mtime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerma"></a>

- `-newerma ARG` - match when mtime is newer than the reference file's atime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newermc"></a>

- `-newermc ARG` - match when mtime is newer than the reference file's ctime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newermm"></a>

- `-newermm ARG` - match when mtime is newer than the reference file's mtime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerat"></a>

- `-newerat ARG` - match when atime is newer than a time string _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerct"></a>

- `-newerct ARG` - match when ctime is newer than a time string _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newermt"></a>

- `-newermt ARG` - match when mtime is newer than a time string _(test, find)_
  The `-newerXt` time-string form: matches when the entry's mtime is newer than TIME - a timestamp xff parses (an ISO date / date-time, @epoch, or a relative span), interpreted in `--timezone` - rather than a reference file. `-newerat` / `-newerct` / `-newerBt` are the access / status-change / birth-time counterparts; the file-reference forms are -newerXY (see `-newer`).
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerba"></a>

- `-newerBa ARG` - match when birth time is newer than the reference file's atime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerbc"></a>

- `-newerBc ARG` - match when birth time is newer than the reference file's ctime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerbm"></a>

- `-newerBm ARG` - match when birth time is newer than the reference file's mtime _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerbb"></a>

- `-newerBB ARG` - match when birth time is newer than the reference file's birth time _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerbt"></a>

- `-newerBt ARG` - match when birth time is newer than a time string _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newerab"></a>

- `-neweraB ARG` - match when atime is newer than the reference file's birth time _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newercb"></a>

- `-newercB ARG` - match when ctime is newer than the reference file's birth time _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-newermb"></a>

- `-newermB ARG` - match when mtime is newer than the reference file's birth time _(test, find)_
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-mtime"></a>

- `-mtime ARG` - match the data-modification age in days _(test, find)_
  Matches the data-modification age. A bare integer N counts 24-hour periods with any fraction floored (a 2.9-day file is 2); `+N` matches strictly older than N units, `-N` strictly younger. A trailing s/m/h/d/w overrides the unit BSD-style (`-mtime -1h` = under an hour old). The xff-only word/compound span (`-mtime "-3 weeks 3 hours"`, sign required) reaches back a full relative duration and is rejected by `--config=find`. See `-mmin` for the minute scale, `-atime` / `-ctime` / `-Btime` for the other time axes.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-mmin"></a>

- `-mmin ARG` - match the data-modification age in minutes _(test, find)_
  The minute-scale `-mtime`: N counts whole minutes (floored), `+N` / `-N` for older / younger. Integer only - no unit suffix and no compound span (use `-mtime` for those).
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-atime"></a>

- `-atime ARG` - match the access age in days _(test, find)_
  `-mtime` measured on the access time (atime): same N-day scale, `+N` / `-N` polarity, BSD unit suffix, and xff compound span. Note atime is often unreliable - many mounts use relatime or noatime, so a read may not update it.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-amin"></a>

- `-amin ARG` - match the access age in minutes _(test, find)_
  The minute-scale `-atime` (access time): integer minutes, `+N` / `-N`, no suffix. See `-mmin`.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-ctime"></a>

- `-ctime ARG` - match the status-change age in days _(test, find)_
  `-mtime` measured on the status-change time (ctime) - when the inode metadata last changed (permissions, ownership, link count, rename), which a content edit also bumps. Same N-day scale, `+N` / `-N` polarity, BSD unit suffix, and xff compound span. This is not a creation time; see `-Btime` for that.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-cmin"></a>

- `-cmin ARG` - match the status-change age in minutes _(test, find)_
  The minute-scale `-ctime` (status-change time): integer minutes, `+N` / `-N`, no suffix. See `-mmin`.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-btime"></a>

- `-Btime ARG` - match the birth (creation) age in days _(test, find)_
  `-mtime` measured on the birth (creation) time: same N-day scale, `+N` / `-N` polarity, BSD unit suffix, and xff compound span. Birth time is not recorded on every filesystem or kernel - where it is absent the test cannot be evaluated and is a hard error (exit 2); `--skip-unsupported` downgrades that to a warning and skips the entry.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-bmin"></a>

- `-Bmin ARG` - match the birth (creation) age in minutes _(test, find)_
  The minute-scale `-Btime` (birth time): integer minutes, `+N` / `-N`, no suffix. Same unrecorded-birth-time handling as `-Btime` (hard error, or a skip under `--skip-unsupported`).
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-used"></a>

- `-used ARG` - match the whole days between atime and ctime _(test, find)_
  Matches the whole days between an entry's last status change and its last access (atime minus ctime) - roughly how long after its metadata changed it was next read. `+N` / `-N` for more / fewer days. Shares atime's relatime / noatime caveat (see `-atime`).
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-perm"></a>

- `-perm ARG` - match the permission bits (octal or symbolic mode) _(test, find)_
  Matches the permission (and setuid / setgid / sticky) bits. MODE is octal (`644`, `0755`) or a chmod-style symbolic mode (`u+w`, `go=r`, comma-separated clauses). A bare MODE matches exactly; `-MODE` matches when ALL the listed bits are set; `/MODE` (GNU) when ANY are. BSD `+octal` is any-of like `/`, while a symbolic `+r` stays exact. Example: `-perm -u+x` = owner-executable. Contrast `-readable` / `-writable` / `-executable`, which probe the effective user's real access.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-maxdepth"></a>

- `-maxdepth ARG` - descend at most N directory levels below each start _(test, find)_
  Limits traversal to at most N levels below each start point: level 0 is a start point itself, 1 its immediate children. Like find this is a global positional option - it applies to the whole run wherever it sits in the expression, not just to what follows it. Pair with `-mindepth` to bound both ends.
  See also: [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="primary-mindepth"></a>

- `-mindepth ARG` - skip entries fewer than N levels below each start _(test, find)_
  Skips entries fewer than N levels below a start point, so `-mindepth` 1 excludes the start points themselves. A global positional option like `-maxdepth` (applies run-wide).
  See also: [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="primary-depth"></a>

- `-depth` - process a directory's contents before the directory _(test, find)_
  Visits a directory's contents BEFORE the directory itself (post-order), so a directory is acted on only after everything within it - what `-delete` needs, and `-delete` turns this on for you. A global positional option; `-d` is the BSD/GNU short spelling.
  See also: [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="primary-d"></a>

- `-d` - BSD/GNU short spelling of -depth _(test, find)_
  See also: [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="primary-xdev"></a>

- `-xdev` - do not descend into other filesystems _(test, find)_
  Confines the walk to the filesystem of each start point: it will not descend into a directory that lives on a different mounted device. A global positional option; `-mount` and `-x` are synonyms.
  See also: [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="primary-mount"></a>

- `-mount` - GNU/BSD synonym for -xdev _(test, find)_
  See also: [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="primary-x"></a>

- `-x` - BSD synonym for -xdev _(test, find)_
  See also: [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="primary-daystart"></a>

- `-daystart` - measure age tests from today's local midnight _(test, find)_
  Measures the day- and minute-scale age tests (`-mtime` / `-atime` / `-ctime` / `-Btime` and their -min forms) from the start of today (local midnight) instead of from the exact current instant, matching GNU find's `-daystart`. Unlike find, where it only affects tests to its right, in xff it applies run-wide regardless of where it appears in the expression.
  See also: [Time formats](#topic-time), [Fields](#topic-fields)

<a id="primary-ignore-readdir-race"></a>

- `-ignore_readdir_race` - skip entries that vanish during the walk (ENOENT) _(test, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-noignore-readdir-race"></a>

- `-noignore_readdir_race` - report vanished entries as errors (default) _(test, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-empty"></a>

- `-empty` - match an empty regular file or empty directory _(test, find)_
  Matches an empty regular file (size 0) or a directory with no entries; other types never match. The directory case reads the directory to check, so it costs a syscall.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-sparse"></a>

- `-sparse` - match a file with holes (allocated blocks < apparent size) _(test, find)_
  Matches a file stored sparsely - fewer 512-byte blocks are allocated than its apparent size would need (`st_blocks * 512 < st_size`), i.e. it has holes. A zero-size file is never sparse. Compare `-blocks` (allocated space) against `-size` (apparent size).
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-readable"></a>

- `-readable` - match entries the current user can read _(test, find)_
  Matches when the entry is readable by the CURRENT (effective) user, via a real access(2) probe rather than a guess from the mode bits - so it reflects ownership and ACLs and can differ from `-perm`. See `-writable` / `-executable` for the other access modes.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-writable"></a>

- `-writable` - match entries the current user can write _(test, find)_
  The write-mode `-readable`: a real access(2) probe for the effective user (see `-readable`).
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-executable"></a>

- `-executable` - match entries the current user can execute _(test, find)_
  The execute/search-mode `-readable`: a real access(2) probe for the effective user. On a directory this means search (traverse) permission. See `-readable`.
  See also: [Safety](#topic-safety), [Fields](#topic-fields), [Configuration](#topic-config)

<a id="primary-true"></a>

- `-true` - always match _(test, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-false"></a>

- `-false` - never match _(test, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

### Actions

<a id="primary-collect"></a>

- `-collect[:[!]NAME]` - add the entry to a named collection for --summary to reduce (xff) _(action, xff)_
  xff extension: an ACTION that adds the entry to a collection instead of printing it, and makes `--summary` reduce THAT collection rather than what matched. This is what a truncating test cannot do on its own: a FALSE test removes the entry from every sink, so `-first 10 --summary` summarises ten entries, never "all of them, showing ten". ORDER selects the reading, because these are primaries rather than position-independent globals: `-collect -first 10 -ls --summary` collects everything, lists ten, and summarises ALL of them, while `-first 10 -collect --summary` collects only the ten and summarises those. The second prints no listing because `-collect` is an action, so the implicit `-print` is suppressed - find's own rule, not a new one. `-collect:NAME` uses a second collection; a bare `-collect` uses the one named `default`. A NAME is an identifier (`[A-Za-z_][A-Za-z0-9_]*`), which is what reserves punctuation for modifiers. Two nodes MAY share a collection, but the later one must SAY so with `!`: `\( -type f -collect:all \) -o \( -type d -collect:!all \)` gathers both branches into one collection, while an unmarked repeat is a usage error - a silently shared collection shows up only as a doubled total. The modifier is per node, so it cannot loosen the other `-collect` in a long command the way a whole-run flag would. A collection holds every match until the walk ends, so `--buffer` bounds it (a row count or a byte budget); exceeding it is an ERROR rather than a silent truncation, because a summary over part of the walk is indistinguishable from a correct one. Without `--buffer` there is no cap. Presence is SYNTACTIC, like the implicit print: a `-collect` in a branch that never runs still switches the summary's source, and the summary is then empty. Example: `xff . -type f -collect -first 3 -ls --summary`.
  See also: [Statistics](#topic-stats), [Output](#topic-output)

<a id="primary-diff"></a>

- `-diff[:STYLE] TARGET` - diff the file against TARGET (a field template); true when equal (xff) _(action, xff)_
  Compares the matched file against TARGET - a {field} template evaluated per entry, so it can name a mirror path like `../b/{relpath}` - and is true when they are equal, false on a difference. The optional :STYLE picks the output: unified `u3` (default; 3 lines of context), context `c`, normal `n`, side-by-side `y`, or `none` for just the boolean. This is a one-sided expression action: it visits only the search roots, so it cannot report paths that exist only under TARGET. Use `--compare[=status|diff] LEFT RIGHT` to walk two roots with the same options and expression, then compare their matches symmetrically. Text files only; expensive.
  Affected by: --diff-algorithm, --diff-ignore, --diff-ignore-matching, --diff-format, --diff-context, --context
  See also: [Comparing trees](#topic-compare), [Content](#topic-content), [--diff-algorithm](#flag-diff-algorithm), [--diff-ignore](#flag-diff-ignore), [--diff-ignore-matching](#flag-diff-ignore-matching), [--diff-format](#flag-diff-format), [--diff-context](#flag-diff-context), [--context](#flag-context)

<a id="primary-hash"></a>

- `-hash[:ALGO[/ENCODING]]` - print the file digest and path; -hash:ALGO[/ENCODING], sha256 hex default (xff) _(action, xff)_
  Prints `DIGEST  PATH` for each match (an action). `-hash:ALGO[/ENCODING]` picks the algorithm (sha256 default; also sha1/sha512/...) and encoding (hex default, or base64). Reads the whole file, so it is expensive; the same digest is available as the {hash} field.
  See also: [Content](#topic-content), [Fields](#topic-fields)

<a id="primary-ls"></a>

- `-ls` - print an `ls -dils` style line per entry _(action, find)_
  Prints one `ls -dils`-style line per match: inode, blocks, mode, links, owner, group, size, time, name (find's `-ls`). Columns align to ls/BSD width defaults. For a custom layout use `-printf`; for aligned columns of {field}s use `--format=aligned`.
  See also: [Output](#topic-output), [Fields](#topic-fields)

<a id="primary-print"></a>

- `-print` - print the path followed by a newline _(action, find)_
  Prints the path then a newline. This is the DEFAULT action: with no action anywhere in the expression xff prints each match, exactly as if `-print` were appended. Naming any action (including `-print` itself) suppresses that implicit default; `--implicit-print=yes`|no forces it on or off.
  See also: [Output](#topic-output), [Fields](#topic-fields)

<a id="primary-print0"></a>

- `-print0` - print the path followed by a NUL _(action, find)_
  Prints the path then a NUL byte instead of a newline, so paths containing spaces or newlines survive a pipe into `xargs -0`. The machine-readable counterpart of `-print`; see also `--format=jsonl`.
  See also: [Output](#topic-output), [Fields](#topic-fields)

<a id="primary-printf"></a>

- `-printf ARG` - print a custom format string (%{field} expands the xff field vocabulary) _(action, find)_
  Prints FORMAT for each match, expanding find's `%` directives (%p path, %f name, %s size, %t/%Ak times, ...) and C escapes (\n, \t). xff adds `%{NAME}` to reach the full {field} vocabulary and its qualifiers (see --help=fields, --help=printf). No trailing newline unless you write one; `-printfln` adds the OS line ending. Example: `xff . -printf '%s\t%p\n'`.
  See also: [Printf directives](#topic-printf), [Fields](#topic-fields), [Output](#topic-output)

<a id="primary-println"></a>

- `-println` - print the path with the OS line ending (xff) _(action, xff)_
  `-print` but terminated with the OS-native line ending (CRLF on Windows, LF elsewhere) rather than always LF. An xff extension `--config=find` rejects.
  See also: [Output](#topic-output), [Fields](#topic-fields)

<a id="primary-printfln"></a>

- `-printfln ARG` - print a custom format with the OS line ending (xff) _(action, xff)_
  `-printf` plus the OS line ending appended, so you write FORMAT without a trailing `\n`. An xff extension `--config=find` rejects; see `-printf` for the directive vocabulary.
  See also: [Printf directives](#topic-printf), [Fields](#topic-fields), [Output](#topic-output)

<a id="primary-grep"></a>

- `-grep[:FORMAT] PATTERN` - print each content line matching a regex; -grep:FORMAT for a template (xff) _(action, xff)_
  The line-output companion of `-rxc`: `-grep PATTERN` prints every content line matching the RE2 PATTERN as `path:lineno:text` (grep's piped form; a literal substring under `--regextype=EXACT`). `-grep:FORMAT PATTERN` renders a {line}/{text}/{match}/{column} template instead. Honors `-c` / `--count` (one `path:count` per file) and -A / -B / `--context` (surrounding lines, grep-style). Reads the file (expensive); non-regular / unreadable / binary files yield nothing. Its truth is "matched a line", so it composes with `-o` / `-q`. An xff extension `--config=find` rejects.
  Affected by: -E, --regextype, --re2, --pcre, --count, --context, --after-context, --before-context
  See also: [Content](#topic-content), [-E](#flag-e), [--regextype](#flag-regextype), [--re2](#flag-re2), [--pcre](#flag-pcre), [--count](#flag-count), [--context](#flag-context), [--after-context](#flag-after-context), [--before-context](#flag-before-context)

<a id="primary-fprint"></a>

- `-fprint ARG` - write -print output to a named file _(action, find)_
  Writes what `-print` would emit to FILE instead of stdout. FILE is opened once (truncating any existing content) and held open for the whole walk, so matches append to it in visit order. This is the anchor of the -f* family - each mirrors a stdout action: `-fprint0`, `-fprintf`, `-fls`, and the xff `-fprintln` / `-fprintfln`.
  See also: [Output](#topic-output), [Fields](#topic-fields), [Safety](#topic-safety)

<a id="primary-fprintln"></a>

- `-fprintln ARG` - write -println output to a named file (xff) _(action, xff)_
  The file form of `-println` (`-fprint` with the OS line ending). See `-fprint` for the file handling; an xff extension `--config=find` rejects.
  See also: [Output](#topic-output), [Fields](#topic-fields), [Safety](#topic-safety)

<a id="primary-fprint0"></a>

- `-fprint0 ARG` - write -print0 output to a named file _(action, find)_
  The file form of `-print0` (NUL-terminated paths). See `-fprint` for the file handling.
  See also: [Output](#topic-output), [Fields](#topic-fields), [Safety](#topic-safety)

<a id="primary-fprintf"></a>

- `-fprintf ARG ARG` - write -printf output to a named file _(action, find)_
  The file form of `-printf`: `-fprintf FILE FORMAT` (FILE first, then the format). See `-printf` for the directive vocabulary and `-fprint` for the file handling.
  See also: [Printf directives](#topic-printf), [Fields](#topic-fields), [Output](#topic-output), [Safety](#topic-safety)

<a id="primary-fprintfln"></a>

- `-fprintfln ARG ARG` - write -printfln output to a named file (xff) _(action, xff)_
  The file form of `-printfln`: `-fprintfln FILE FORMAT` with the OS line ending appended. An xff extension `--config=find` rejects; see `-fprint` for the file handling.
  See also: [Printf directives](#topic-printf), [Fields](#topic-fields), [Output](#topic-output), [Safety](#topic-safety)

<a id="primary-fls"></a>

- `-fls ARG` - write -ls output to a named file _(action, find)_
  The file form of `-ls` (the `ls -dils` line). See `-fprint` for the file handling.
  See also: [Output](#topic-output), [Fields](#topic-fields), [Safety](#topic-safety)

<a id="primary-delete"></a>

- `-delete` - delete the matched entry _(action, find, modifies the filesystem)_
  Deletes the matched file or (empty) directory, and implies `-depth` so a directory's contents are removed before the directory itself. Destructive, so it is guarded: `--dry-run` previews (prints what would be deleted, removes nothing) and `--safe` refuses risky targets. Example: `xff . -name '*.tmp' -delete`.
  See also: [Safety](#topic-safety), [Archives](#topic-archive)

<a id="primary-prune"></a>

- `-prune` - do not descend into the matched directory _(action, find)_
  When the matched entry is a directory, do not descend into it (evaluates true). Usually paired with `-o` to skip a subtree while still processing everything else: `xff . -name .git -prune -o -print`.
  See also: [Ignore and VCS traversal](#topic-ignore), [Archives](#topic-archive)

<a id="primary-quit"></a>

- `-quit` - stop the search immediately _(action, find)_
  Stops the whole search as soon as it is reached (after actions on the current entry have run). Handy to emit just the first match: `xff . -name target -print -quit`.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-exec"></a>

- `-exec CMD... ;` - run a command per match (;) or batched (+) _(action, find, runs commands)_
  Runs the command up to a terminator: `;` runs it once per match, `+` batches as many paths as fit per invocation (like xargs). `{}` expands to the path; xff also binds `{1}`..`{N}` from `-regex` capture groups and the whole {field} vocabulary. The `;` form is synchronous at `-j 1`; larger job counts permit up to `N` children concurrently, and their direct stdout / stderr may interleave. The `+` form remains an end-of-walk batch. Sensitive: loaded from an `--xffrc` file it needs `--allow-exec`. Example: `xff . -name '*.o' -exec rm {} +`.
  See also: [Safety](#topic-safety), [Fields](#topic-fields), [Configuration](#topic-config)

<a id="primary-execdir"></a>

- `-execdir CMD... ;` - run a command in the matched entry's directory _(action, find, runs commands)_
  Like `-exec`, but each command runs with its working directory set to the matched entry's parent and `{}` is the basename - safer against path injection and directory races. `;` per match or `+` batched (a batch shares one directory). Example: `xff . -name '*.log' -execdir gzip {} ;`.
  See also: [Safety](#topic-safety), [Fields](#topic-fields), [Configuration](#topic-config)

<a id="primary-ok"></a>

- `-ok CMD... ;` - like -exec, but prompt before each command _(action, find, runs commands)_
  Like `-exec` but prompts on stderr before each command and runs it only when the reply begins with 'y'; a declined or EOF answer skips that entry. `;`-terminated only (no `+` batching, since each run needs its own prompt).
  See also: [Safety](#topic-safety), [Fields](#topic-fields), [Configuration](#topic-config)

<a id="primary-okdir"></a>

- `-okdir CMD... ;` - like -execdir, but prompt before each command _(action, find, runs commands)_
  Like `-execdir` (runs in the matched entry's directory, `{}` is the basename) but prompts before each command, exactly as `-ok` does.
  See also: [Safety](#topic-safety), [Fields](#topic-fields), [Configuration](#topic-config)

<a id="primary-capture"></a>

- `-capture:[!]NAME[=REGEX] CMD... ;` - run a command and bind its output to {capture.NAME} (xff) _(action, xff, runs commands)_
  xff extension: runs the `;`-terminated command and binds its stdout to `{capture.NAME}` for a later `-printf` / `--format` field; `-capture:NAME=REGEX` keeps only REGEX's first capture group. A NAME must be an identifier (`[A-Za-z_][A-Za-z0-9_]*`), because it is referenced as `{capture.NAME}`; binding one NAME twice is an error, and `-capture:!NAME` on the LATER node says the re-bind is meant (per node, so it cannot loosen the other captures in the command). Sensitive: from an `--xffrc` file it needs `--allow-exec`. Example: `-capture:branch git rev-parse --abbrev-ref HEAD ; -printf '{relpath}\t{capture.branch}\n'`.
  Affected by: -E, --regextype, --re2, --pcre
  See also: [Safety](#topic-safety), [Fields](#topic-fields), [Configuration](#topic-config), [-E](#flag-e), [--regextype](#flag-regextype), [--re2](#flag-re2), [--pcre](#flag-pcre)

<a id="primary-capturedir"></a>

- `-capturedir:[!]NAME[=REGEX] CMD... ;` - run -capture in the matched entry's directory (xff) _(action, xff, runs commands)_
  The `-execdir` counterpart of `-capture`: runs the command in the matched entry's directory and binds its stdout to `{capture.NAME}`. Same `NAME[=REGEX]` binding and `--allow-exec` gating.
  Affected by: -E, --regextype, --re2, --pcre
  See also: [Safety](#topic-safety), [Fields](#topic-fields), [Configuration](#topic-config), [-E](#flag-e), [--regextype](#flag-regextype), [--re2](#flag-re2), [--pcre](#flag-pcre)

### Operators

<a id="primary-a"></a>

- `-a` - logical AND (implicit between predicates) _(operator, find)_
  Logical AND of two predicates (`-and` is the long spelling). It is also IMPLICIT between juxtaposed predicates, so `-type f -name '*.c'` means `-type f -a -name '*.c'`. Precedence, tightest to loosest: `-not`, then `-a`, then (xff) `-xor`, then `-o`, then the `,` comma operator; parentheses `( ... )` override it. Evaluation short-circuits.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-and"></a>

- `-and` - logical AND (implicit between predicates) _(operator, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-o"></a>

- `-o` - logical OR _(operator, find)_
  Logical OR of two predicates (`-or` is the long spelling); binds looser than `-a`, so `A -o B -a C` is `A -o (B -a C)`. Short-circuits: the right side is skipped when the left already matched. See `-a` for the full precedence order.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-or"></a>

- `-or` - logical OR _(operator, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-not"></a>

- `-not` - logical negation _(operator, find)_
  Negates the predicate that follows (`!` is the synonym). Binds tightest of the operators, so `-not -type d -o -name x` is `(-not -type d) -o -name x`. See `-a` for the full precedence order.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary"></a>

- `!` - logical negation _(operator, find)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-xor"></a>

- `-xor` - logical XOR; matches exactly one side (xff) _(operator, xff)_
  Matches when exactly ONE side is true (never both). One of four xff-only operators find lacks: `-xor`, and the negations `-nand` (not both), `-nor` (neither), `-xnor` (both agree). They sit between `-a` and `-o` in precedence (`-not` > `-a` / `-nand` > `-xor` / `-xnor` > `-o` / `-nor`) and, like all xff-only operators, are rejected by `--config=find`.
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-nand"></a>

- `-nand` - logical NAND; ! (lhs -a rhs) (xff) _(operator, xff)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-nor"></a>

- `-nor` - logical NOR; ! (lhs -o rhs) (xff) _(operator, xff)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

<a id="primary-xnor"></a>

- `-xnor` - logical XNOR; matches when both sides agree (xff) _(operator, xff)_
  See also: [Expression](#topic-expressions), [Examples](#topic-cookbook)

See also: [Regex matching](#topic-regex), [Content](#topic-content), [Output](#topic-output), [Safety](#topic-safety)

<a id="topic-output"></a>

## Output

When the expression contains no action, xff implicitly lists every match. An explicit action such as `-print`, `-grep`, or `-exec` suppresses that default listing unless `--implicit-print=yes` restores it. `--summary`, `--histogram`, and `--pack` are terminal sinks: they replace the implicit per-match listing but do not suppress actions written in the expression.

### Formats

- `plain` - one path plus newline; streaming; use `--path-encoding=escape` for visible control bytes
- `nul` - raw paths separated by NUL; streaming and safe for arbitrary path bytes
- `jsonl` - one JSON object per record; the default listing uses a `path` member
- `csv` - RFC 4180 rows; streaming; header and `--columns` supported
- `tsv` - tab-separated rows with tabs, newlines, carriage returns, and backslashes escaped
- `aligned` - human-readable columns; buffers rows to determine display widths
- `markdown` - a GitHub-flavored Markdown table; also spelled `md`; buffered
- `tree` - an indented path hierarchy; buffered; Unicode or ASCII connectors per `--unicode`

`--columns=FIELD,...` applies only to `csv`, `tsv`, `aligned`, and `markdown`; the default column is `path`. Those formats include a header unless `--no-header` is set. `--template` is the free-form alternative for a custom per-entry record; see `--help=fields` for placeholders.

### Filename safety

A Unix path may contain tabs and newlines. Plain output preserves those bytes by default, so it is for people rather than reliable parsing. Prefer `--format=nul` for shell pipelines, `jsonl` for structured consumers, or a tabular format whose escaping rules are defined above. `--path-encoding=escape` makes control bytes visible in plain output but is not a reversible record separator.

### Examples

```sh
xff . -type f --format=nul | xargs -0 sha256sum --
```

pass arbitrary filenames safely to another command

```sh
xff . -type f --format=csv --columns=path,size,mtime
```

stream selected fields as CSV with a header row

```sh
xff . -type f --template='{size:human}  {path}'
```

render a custom human-readable record for each match

See also: [Fields](#topic-fields), [Printf directives](#topic-printf), [Statistics](#topic-stats), [Comparing trees](#topic-compare), [Safety](#topic-safety)

<a id="topic-fields"></a>

## Fields

The `{field}` placeholder vocabulary, substituted per entry in `--template` / `--format`, in `-printf` via the `%{field}` escape, and (with `--exec-fields`) in `-exec`.

### Path & name

- `{path}` - full path as traversed ({} is an alias)
- `{relpath}` - path relative to the search root (find %P)
- `{root}` - the search root it was reached from (find %H)
- `{dir}` - directory containing the entry
- `{name} {file}` - final path component (the file name)
- `{stem}` - name without its last extension
- `{core}` - name without all extensions (foo.tar.gz -> foo)
- `{ext} {extension}` - last extension, no dot (gz)
- `{suffix}` - last extension, with dot (.gz)
- `{suffixes}` - all extensions, with dots (.tar.gz)
- `{target}` - a symlink's target (find %l); else empty

### Type & size

- `{type}` - entry type letter (f, d, l, ...)
- `{lang} {language}` - language by extension/filename (C++, Python, ...; empty if unknown)
- `{lang-type}` - language kind (programming, markup, data, or prose); empty when unspecified
- `{lang-color}` - language display colour (#RRGGBB); empty when unspecified
- `{lang-group}` - parent language group; empty when the language is not grouped
- `{lang-source}` - vocabulary provenance for the language; empty when unspecified
- `{mime}` - media (MIME) type by extension (text/plain, image/png; application/octet-stream if unknown)
- `{mime-category}` - top-level media category (application, image, text, ...)
- `{mime-description}` - media-type description from the active vocabulary; empty when unspecified
- `{mime-charset}` - default media-type charset; empty when unspecified
- `{mime-compressible}` - whether the media type is normally compressible (yes/no; empty when unknown)
- `{mime-source}` - vocabulary provenance for the media type; empty when unspecified
- `{size}` - size in bytes ({size:h} human-readable)
- `{blocks}` - 512-byte blocks allocated
- `{inode}` - inode number
- `{links}` - hard-link count
- `{dev}` - device number
- `{depth}` - depth below the root (0 at a root operand)

### Content

- `{hash}` - file digest; {hash:ALGO[/ENCODING]} picks the algorithm (default sha256) and hex/base64
- `{lines}` - text line count (empty for a binary/unreadable file); reads the file
- `{fuzzy}` - normalized fuzzy quality (AND = weakest requirement, OR = best alternative)
- `{shard}` - with --shards, the number of shards in the set (empty otherwise); size-like fields then aggregate across the set

### Owner & mode

- `{user} {owner}` - owner user name (alias {owner}; find %u)
- `{group}` - owner group name
- `{uid}` - owner numeric user id
- `{gid}` - owner numeric group id
- `{mode} {perm}` - permission bits, octal
- `{access}` - symbolic permissions (ls -l / stat %A)

### Time

- `{atime}` - last access time
- `{mtime}` - last modification time
- `{ctime}` - inode change time
- `{btime}` - creation/birth time (where supported)

### Grep context

- `{line}` - 1-based number of the matching line
- `{text}` - the full matching line
- `{match}` - the matched substring (grep -o)
- `{column}` - 1-based column of the match

### Braces

- `{{` and `}}` emit literal braces
- `{}` is an alias for `{path}`
- an unknown field renders empty
- a malformed or unterminated `{` stays literal

### Dynamic namespaces

- `{0}..{N}` - -regex captures ({0} the whole match, {1}..{N} the groups)
- `{env.NAME}` - a process environment variable
- `{def.NAME}` - a --define value
- `{capture.NAME}` - a -capture command result

### Qualifiers ({field:QUAL})

- `{mtime:FMT}` - time format: strftime (%Y-%m-%d) or preset (iso, epoch); see --time-format / --timezone
- `{size:h}` - human-readable size
- `{name:s/RE/R/f}` - RE2 rewrite of the value (flags g=all, i=ignore-case; any delimiter)
- `{cap:m/RE/R/f}` - per-line extraction: a value stream, e.g. a --summary key (m//, s///'s list-producing sibling)
- `{cap:m/RE/R/;join(SEP)}` - reduce the stream to one scalar (join, SEP default newline) so m// is usable in a scalar context (-printf / --template / -exec); reducers are function-notation, e.g. join(, )
- `{path:COMP}` - path component of the value: basename|core|dir|ext|extension|file|name|path|stem|suffix|suffixes; any path-valued field composes, e.g. {relpath:stem}, {def.B:dir}

An m// extraction is a left-to-right pipeline: s/// maps whatever is flowing (each line, then the scalar), and a terminal reducer such as join collapses the stream to one scalar.

```
  {cap:m/PAT/REP/;s/PAT/REP/;join(SEP);s/PAT/REP/}
       |________| |________| |_______| |________|
       extract    map each   reduce    rewrite
       per line   line       stream    scalar
```

For -printf's own % directives (%p %f %s %t ...) and the `%{field}` escape that bridges them to this vocabulary, see the Printf directives (`--help=-printf`).

See also: [Printf directives](#topic-printf), [Output](#topic-output), [Time formats](#topic-time), [Content](#topic-content)

<a id="topic-printf"></a>

## Printf directives

Directives for `-printf` / `-fprintf` / `-printfln` / `-fprintfln` FORMAT, and the `%{field}` escape.

- `%p` - the entry's path
- `%f` - file name (basename)
- `%h` - leading directories (dirname)
- `%d` - depth below the starting point
- `%y` - type letter (f d l b c p s)
- `%s` - size in bytes
- `%i` - inode number
- `%n` - number of hard links
- `%m` - permission bits, octal
- `%u` - owner user name (numeric uid if unknown)
- `%g` - owner group name (numeric gid if unknown)
- `%U` - numeric user id
- `%G` - numeric group id
- `%a %c %t` - access / change / modification time (asctime form)
- `%Ak %Ck %Tk` - atime / ctime / mtime via strftime conversion k (e.g. %TY, %Tj)
- `%%` - a literal percent
- `\n \t \r \\ \0` - newline, tab, carriage return, backslash, NUL
- `%{NAME}` - xff: the {field} vocabulary (%{relpath}, %{size:h}, %{def.X}); see --help=fields
- `%{NAME:qual}` - xff: a field with a :qualifier -- time format, {size:h}, s/// rewrite, or path component (see --help=fields for the full qualifier list)

See also: [Fields](#topic-fields), [Output](#topic-output), [Time formats](#topic-time)

<a id="topic-time"></a>

## Time formats

Presets and strftime patterns for --time-format, --timezone, and time-field {:qualifiers}.

- `iso, iso8601` - ISO-8601 extended (2020-09-13T12:26:40+0000)
- `iso8601-basic` - ISO-8601 basic / compact (20200913T122640+0000)
- `iso8601-full` - ISO-8601 with sub-second precision
- `rfc3339` - RFC 3339, colon offset (2020-09-13T12:26:40+00:00)
- `space, human` - readable default (2020-09-13 12:26:40 +0000)
- `asctime` - asctime(3); find's default %t (Sun Sep 13 12:26:40 2020)
- `epoch` - seconds since the Unix epoch
- `zulu` - UTC with a Z designator (2020-09-13T12:26:40Z)
- `zulu-dense` - UTC Z, no separators (20200913T122640Z)
- `asn1, generalizedtime` - ASN.1 GeneralizedTime, local (20200913122640); =always adds +0000
- `asn1z` - ASN.1 GeneralizedTime, UTC Z (20200913122640Z)
- `<strftime>` - any other value is used as an strftime(3) pattern, e.g. %Y-%m-%d

Time zone. `--timezone=ZONE` (alias `--tz`) sets the zone every time is interpreted and rendered in: `local` (the default), `utc` (also `z` / `zulu`), an IANA name like `Europe/London`, or a fixed offset like `+02:00` / `-0800`. It shifts the wall-clock digits of every time field and governs `-newerXt` comparisons; it does not by itself add or remove the printed zone suffix.

Zone suffix. `--time-zone-suffix=never` drops the trailing offset (`+0100`, `+01:00`) from a preset that shows it by default (`space`, `iso` / `iso8601-*`, `rfc3339`); `always` forces one on, even onto `asctime` which omits it; `auto` (the default) keeps each preset's built-in behavior. `true` / `false` are accepted for `always` / `never`. Two things it never touches: the inherently-zoned `zulu` / `zulu-dense` / `asn1z` keep their mandatory `Z` (UTC is the format's identity), and a custom strftime `--time-format` is left exactly as written - control its zone there with `%z` / `%Ez` / `%Z` yourself. `asn1`'s zone is optional, so `always` appends its ASN.1-style offset (`+0100`, no separator) and `never` / `auto` leave it bare.

See also: [Fields](#topic-fields), [Printf directives](#topic-printf)

<a id="topic-size"></a>

## Size units

Units for -size / -blocks [+|-]N[unit]; spell SI and IEC explicitly.

- `c` - legacy byte unit
- `w` - legacy 2-byte word unit
- `b / bare` - blocks (512 bytes by default; --block-size overrides)
- `k / M / G / T / P / E` - legacy binary units (1024 through 1024^6 bytes)
- `B` - bytes (explicit)
- `kB / MB / GB / TB / PB / EB` - SI units (powers of 1000)
- `KiB / MiB / GiB / TiB / PiB / EiB` - IEC units (powers of 1024)
- `+N / -N` - greater than / less than N units; a bare N matches exactly

All byte counts use an unsigned 64-bit value, so the largest representable size is `18446744073709551615 B` (about `18.45 EB`, just under `16 EiB`). A number multiplied by its unit must fit that range: `18EB` and `15EiB` fit, while `19EB` and `16EiB` overflow and are usage errors. `ZB` and `ZiB` are not accepted because one zettabyte/zebibyte already exceeds the representation; listing their suffixes would promise a unit for which no positive whole value can be represented.

See also: [Statistics](#topic-stats), [Fields](#topic-fields)

<a id="topic-grammars"></a>

## Regex grammars

The grammar for `-regex` / `-iregex` and the content matchers `-rxc` / `-grep`, chosen by `--regextype` (default `RE2`). `ERE`, `EXACT`, `FNMATCH`, `GLOB` and `SHGLOB` are core engines, always built in; `PCRE2` is a build-time extra (see `--help=extras`). RE2 and PCRE2 have canonical external references, so the smaller engines are spelled out in full here: they have no single authoritative man page, and ERE and FNMATCH delegate to the platform's regcomp(3) and fnmatch(3), whose locale, class, and collation details vary by system.

- `ERE` - POSIX extended regular expressions through the platform regcomp(3) implementation. This provides traditional find -E syntax, captures, partial matching, and rewrites, but locale details and some edge-case behavior follow the host C library rather than RE2's cross-platform semantics or linear-time guarantee. Patterns containing NUL are errors; subjects containing NUL do not match because the POSIX API uses C strings.
- `EXACT` - a literal string; every character matches itself, no metacharacters. -regex is whole-string equality, -rxc / -grep a substring test.
- `FNMATCH` - a flat shell wildcard via the platform's fnmatch(3): * matches any run of characters (including /), ? one character, [...] a class. Whole-string, like find -name / -path (no /-awareness); -i uses FNM_CASEFOLD. Provided by libc, so class / collation details vary by system.
- `GLOB` - xff's path-aware, locale-independent shell glob (compiled to RE2 - NOT POSIX glob(7)): * and ? stay within one path component; a complete-component ** crosses components (middle foo/**/bar permits zero or more, while trailing foo/** requires a descendant); embedded star runs reduce to *. [...] supports literals, ascending ranges, leading ! negation, and RE2 ASCII named classes, always excluding / except the compatibility spelling [/]. Malformed ranges, descending ranges, unsupported named classes, collation/equivalence, and negative extglob are errors. Braces are literal. Because it compiles to RE2, -grep / -rxc partial matching and match spans work.
- `PCRE2` - Perl-Compatible Regular Expressions (lookaround, backreferences, ...). A build-time extra: present only in a full build - run `xff --help=extras` to see whether THIS binary has it. Full syntax: pcre2pattern(3).
- `RE2` - the default. Google RE2 regular expressions - linear-time, no catastrophic backtracking. Full syntax: https://github.com/google/re2/wiki/Syntax .
- `SHGLOB` - GLOB plus brace alternation: {a,b,c} matches any one alternative, so *.{cc,h} matches either. Integer and ASCII-letter sequences expand in either direction (`{1..9}`, `{09..01}`, `{a..z}`); a leading zero preserves integer width, and expansion above 10,000 terms is rejected. Alternatives and sequences may nest; alternatives may be empty. Escaped braces and commas, braces inside a [...] class, and comma-less braces that are not a sequence are literal. The optional shell increment form (`{1..9..2}`) is not supported and remains literal. Everything else is exactly GLOB.

See also: [Regex matching](#topic-regex), [Content](#topic-content)

<a id="topic-regex"></a>

## Regex matching

Use `-regex` / `-iregex` to match the whole path, `-rxc` / `-irxc` to find a match in file content, and `-grep` for line-oriented content output. `--regextype` selects the grammar; `--case` controls case handling. The `i` variants force case-insensitive matching. Quote patterns so the shell passes their metacharacters to xff unchanged.

### Examples

```sh
xff src --regextype=RE2 -regex '.*[.](cc|h)'
```

```sh
xff src --regextype=RE2 --case=insensitive -grep 'todo|fixme'
```

See also: [Regex grammars](#topic-grammars), [Content](#topic-content)

<a id="topic-content"></a>

## Content

These primaries read the entry's BYTES, not its metadata: `-grep` prints matching lines the way ripgrep does, `-content` / `-icontent` test for a literal, `-rxc` / `-irxc` for a regex (grammar per `--regextype`, see `--help=grammars`), and `-text` / `-eofcr` / `-eofcrlf` classify line endings and completeness. `{lines}`, `{text}`, `{line}`, `{match}` and `{column}` carry the results into templates (`--help=fields`).

Every one of them reads through the entry's OWN filesystem, so under `--archive` a member is searched inside its container exactly like a plain file - `a.tar!notes.txt` greps without unpacking anything. Reading is per entry and streamed, so a match in a huge tree costs the bytes of the files visited, not of the tree.

### Examples

```sh
xff src -name '*.cc' -grep 'TODO\('
```

matching lines, rg-style, from the files an expression picked

```sh
xff . -type f ! -text
```

the files that are NOT line-oriented text

```sh
xff -z logs.tar -grep ERROR --count
```

per-member match counts inside an archive

See also: [Regex matching](#topic-regex), [Fields](#topic-fields), [Output](#topic-output)

<a id="topic-compare"></a>

## Comparing trees

`--compare` requires exactly two roots. xff walks both roots with the same expression and the same explicitly selected traversal, ignore, archive, hidden-file, and symlink options. Comparison does not enable `--gitignore`, `--no-ignore`, `-L`, or any other policy on its own. The matched entries are paired by path relative to their respective roots, so the root directory names need not match.

Regular files are equal when their bytes are equal, for both text and binary data. Unfollowed symlinks are equal when their link targets are equal; `-H` / `-L` instead apply their ordinary traversal meaning. Directories and other non-regular entries are compared by type. Metadata such as permissions, owner, timestamps, and inode numbers is not compared.

The two walks run concurrently and each retains its complete matched-entry inventory until both finish. xff then merges those inventories and emits comparison records in bytewise relative-path order. This final comparison order is fixed: `--sort` controls traversal and the timing of expression actions within each side, not the order of status records or patch entries. Explicit expression actions execute independently for both walks; their synchronized output may interleave. `--buffer` still controls the output and collection buffers documented for those features, but it does not cap the comparison inventories, whose memory use grows with the total number of matched entries.

### Status output

Bare `--compare` (or `--compare=status`) writes tab-separated `STATUS` and relative-path records. The default selection reports discrepancies only; `--compare-select=all` also includes equal entries. `--compare-select=none` (or an empty value) suppresses per-path output without changing summary counts. `--path-encoding=escape` makes control bytes in the path unambiguous.

`--summary` (or `--summary=compare`) appends counts, count percentages, combined sizes, and size percentages by entry type and status, followed by a total. Each paired path counts once; its combined size includes both sides. An identical 100-byte pair contributes one result and 200 bytes; a different 100/150-byte pair contributes one result and 250 bytes. Percentages use all results or all combined bytes respectively; zero denominators produce zero percent. `--summary-precision` controls decimals. `--format=jsonl` emits `type`, `group`, `count`, `count_percent`, `bytes`, and `size_percent`. Directories, symlinks, and special entries participate, including matched roots. Directory equality means entry-kind equality, not subtree equality. A type-changing pair appears once under its left-to-right type transition. Sizes use entry metadata, never recursive directory sizes. Ordinary summary groupings default to `--summary-scope=compare` when `--compare` is active: one left/right table across all categories. Without `--compare`, the default is `all`. An explicit scope overrides this conditional default regardless of option order. Explicit `--summary-scope=all` combines both input trees: its entry count is left-only plus right-only plus twice the paired result count; its byte total equals the comparison's combined bytes for the same population. Ordinary percentages use full table totals before `--top`. `--summary-scope=root` separates roots; `--summary-scope=compare` aligns left and right columns in one table per grouping across the selected categories. Each side's percentages use its full selected category population, before `--top`. Missing groups display a dash (JSON `null`); existing zero-byte files retain numeric zeros. `--format=markdown` (alias `md`) exports comparison-result and ordinary summary tables as Markdown; `--columns` remains a listing-only option.

`--compare=summary` is shorthand for `--compare=status --compare-select=none --summary=compare` at that position in the option sequence. Later selections can enable per-path output, and `--summary=none` can disable the summary.

- `left-only` - the relative path matched only below the left root
- `right-only` - the relative path matched only below the right root
- `different` - both sides matched the path, but its type, bytes, or symlink target differs
- `identical` - both sides matched and compare equal; omitted unless explicitly selected

### Patch output

`--compare=diff` writes one unified patch for added, removed, and changed regular files. Text files get hunks; binary and non-regular differences get a diagnostic line. `--diff-algorithm` and `--diff-context` tune this mode. `identical` is rejected because an unchanged entry has no patch representation.

### See also

- `--summary=compare`: counts, percentages, and combined sizes for all comparison results.
- `--compare-select`: choose per-path records; `none` suppresses the listing.
- `--summary-scope`: combined, per-root, or category statistics.
- `--summary-precision`: decimal places in summary percentages.
- `--format=jsonl`: machine-readable comparison summary rows.

### Examples

```sh
xff --compare=summary left-tree right-tree
```

```sh
xff --compare=summary left-tree right-tree --summary=ext --summary-scope=compare
```

summarize extensions within each comparison category

show only comparison statistics, with no per-path records

```sh
xff --compare left-tree right-tree
```

print only paths present on one side or different on both sides

```sh
xff --compare --compare-select=all left-tree right-tree
```

include `identical` records in the status stream

```sh
xff -g+ --compare=diff old-tree new-tree >changes.patch
```

apply Git ignore rules to both walks and create a patch

```sh
xff -L --compare left-tree right-tree -type f -name '*.dat'
```

follow symlinks and compare only matching regular data files

See also: [Statistics](#topic-stats), [Output](#topic-output), [Content](#topic-content)

<a id="topic-ignore"></a>

## Ignore and VCS traversal

Ignoring a path and pruning version-control metadata are separate decisions. Ignore rules filter ordinary paths by pattern; `--skip-vcs` prevents xff from entering administrative trees such as `.git` or `.hg` at all. Hidden-path filtering is a third independent switch. Changing one does not silently change the others.

### Independent axes

- `--exclude / --include` - command-line gitignore-style patterns; repeatable, later matches win
- `--gitignore / -g` - Git's `.gitignore`, `.git/info/exclude`, and `core.excludesFile` layer
- `--ignore-files` - per-directory `.ignore` and `.xffignore` files
- `--ignore-file=PATH` - an explicitly named rule file, rooted at its own directory
- `--skip-vcs` - prune VCS metadata names; independent of pattern-based ignore files
- `--hidden / --no-hidden` - show or skip dot-prefixed path components

### Defaults and overrides

The find and xff styles start with ignore files off and hidden paths visible. The rg style honours VCS, `.ignore`, and `.xffignore` files and skips hidden paths. Tree comparison does not change these defaults or enable an ignore source: both sides use the selected style and the ignore options the user supplies.

Bare `-g` / `--gitignore` is automatic: it activates only inside a Git working tree. `-g+` / `--gitignore=on` forces the Git layer anywhere; `-g-` / `--gitignore=off` disables it. `--ignore-vcs` and `--no-ignore-vcs` are the rg-style spellings for that same layer. Within this family the last flag wins.

When the Git ignore layer is active, xff also implicitly prunes `.git` as if `--skip-vcs=git` were present. An explicit `--skip-vcs[=LIST]` replaces that implicit choice; bare or `=all` selects every known VCS, while `--no-skip-vcs` / `=none` keeps metadata in the walk. A list such as `--skip-vcs=git,hg` is frozen to exactly those systems.

`--no-ignore` / `-u` is the master off switch for ignore-file sources, including explicit `--ignore-file` inputs. It does not cancel command-line `--exclude` patterns or an explicit `--skip-vcs`. Use `--no-skip-vcs` separately when metadata directories must remain visible.

### Pattern precedence

For a path, command-line `--exclude` / `--include` patterns decide first, then explicit `--ignore-file` sources, then per-directory files. Within a source, gitignore last-match-wins semantics apply; a matched directory is pruned, so a later rule cannot recover descendants that were never visited.

### Examples

```sh
xff -g . -name '*.cc'
```

honour Git rules automatically and search the remaining tree

```sh
xff --skip-vcs=git,hg .
```

prune only Git and Mercurial metadata, without enabling ignore files

```sh
xff -g --no-skip-vcs .
```

honour Git rules while allowing nested `.git` metadata into the walk

```sh
xff -u --skip-vcs .
```

ignore no rule files, but still prune every known VCS metadata tree

See also: [Configuration](#topic-config), [Archives](#topic-archive)

<a id="topic-archive"></a>

## Archives

With `--archive`, an archive is a directory: xff opens it and walks its members as ordinary entries, so every predicate and action applies to them unchanged - `-name`, `-type`, `-grep`, `{hash}`, `--summary`. Nothing in the expression vocabulary knows about archives. Needs at least one container-reader extra; `--help=extras` says which readers this binary has.

### How far diving goes

- `none` - an archive is one plain file (find's behaviour, and the find-style default)
- `roots` - dive only when a search root IS an archive (the xff-family default)
- `all` - dive archives met during the walk too (what a bare `--archive` selects)
- `any` - `all`, and offer EVERY file to the reader rather than only container-looking names

Two axes, spelled independently: the RUNG says how much to look at, the CASE of the short form says whether writing is armed. So a slipped shift key changes the capability, never the level - and arming is not doing: something still has to ask for a write, and `--safe` / `--dry-run` still apply.

Later wins, per axis, which is what makes the two useful together: `-Z++ -z-` arms writing with reading OFF. Permission alone performs no operation: with diving enabled it is consumed by member-mutating actions such as `-delete` or the exec family; with diving off it has no observable effect unless the run also names a creation sink such as `--pack`. Creating a new archive does not itself require member-write permission, because it does not mutate an existing member. `-Z-` is the full reset, turning reading off AND disarming writing whatever an earlier flag or a config file asked for. A lower-case form never disarms; only `-Z-` does.

```text
                   read only    + write (--archive-write)
  none              -z-          -Z-  (also disarms writing)
  roots (default)   -z           -Z
  all               -z+          -Z+
  any               -z++         -Z++
```

Under `all` a file is only opened when its NAME looks like a container, so walking a source tree does not read every file in it; `any` (also spelled `--archive-any`) drops that gate. Nesting has its own cap (`--archive-depth`, default 1) because a container inside a container is where a decompression bomb lives - and it is deliberately NOT part of any rung, since raising the bomb cap is a different decision from looking in more places. `-maxdepth` keeps counting member levels as ordinary depth.

### A member is an entry, a container is still a file

A member's path is the container's, the separator, then the member: `a.tar!dir/two.txt` (`--archive-separator` / `--archive-prefix` spell it differently). The container keeps its own identity at the same time - it is a real `-type f` you can match and delete - so a dive shows you both, which is also why `--archive-aggregate` exists: a reduction that counted the container AND its members would describe no filesystem that exists.

Format-defined file-like parts use that same entry model. A native phar's executable stub is the readable regular entry `.phar/stub.php`, so it can be matched, searched, formatted, and included in `--summary` / `--histogram` like any other member. There is never a second entry with the same path: if the manifest stores `.phar/stub.php` explicitly, that stored member wins over the synthetic view. Incidental metadata that the format does not model as a file is not invented as one. A future visibility option would control presentation of these parts; it must not create duplicate path identities.

An Electron `.asar` bundle follows the same rule: packed files, external `.asar.unpacked` files, directories, and links are entries, while integrity records are metadata and never appear as synthetic files. The ASAR reader is read-only and verifies declared SHA256 whole-file and block hashes when content is read.

A SquashFS image is another read-only virtual filesystem. The independent SquashFS extra covers raw images, Snap packages, and the embedded filesystem in a type-2 AppImage without mounting it; indexed metadata, links, and member contents use the ordinary expression vocabulary.

Members are READ-ONLY by default. `-delete` and the exec family refuse one rather than silently doing nothing, because a member has no path a process can open and no way to be unlinked; `--archive-extract` runs the child over a temporary copy, and `--archive-delete` rewrites the container without the member. Both are opt-in, and both say so in the refusal you get without them.

### Formats this binary understands

Reading is decided by CONTENT (the reader sniffs the bytes), so the extensions are what the name gate dives on under `all` and how the format is usually spelled - a container with an unlisted name still reads under `any`. Package extensions ride their underlying format: a `.jar` is a zip, a `.deb` an ar, an `.rpm` a cpio, `.crate` and `.gem` are tars, and `file` is a compressed SINGLE file (`notes.txt.gz`, one member). Write means `--pack` can create it.

| format   | read | write | extensions                                                                                                                                                            |
| -------- | ---- | ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 7z       | yes  | no    | .7z                                                                                                                                                                   |
| ar       | yes  | no    | .ar, .deb                                                                                                                                                             |
| cab      | yes  | no    | .cab                                                                                                                                                                  |
| cpio     | yes  | no    | .cpio, .rpm                                                                                                                                                           |
| iso9660  | yes  | no    | .iso                                                                                                                                                                  |
| lha      | yes  | no    | .lha, .lzh                                                                                                                                                            |
| rar      | yes  | no    | .rar                                                                                                                                                                  |
| tar      | yes  | yes   | .tar, .tar.gz, .tgz, .taz, .crate, .gem, .tar.bz2, .tbz, .tbz2, .tz2, .tar.xz, .txz, .tlz, .tar.lz, .tar.lzma, .tar.lz4, .tar.Z, .taZ, .tar.zst, .tzst, .tar.br, .tbr |
| warc     | yes  | no    | .warc                                                                                                                                                                 |
| xar      | yes  | no    | .xar                                                                                                                                                                  |
| zip      | yes  | yes   | .zip, .jar, .war, .ear, .whl, .egg, .apk, .aab, .cbz, .crx, .docx, .epub, .jmod, .nupkg, .odp, .ods, .odt, .pptx, .vsix, .xlsx, .xpi                                  |
| phar     | yes  | no    | .phar                                                                                                                                                                 |
| file     | yes  | no    | .gz, .bz2, .xz, .zst, .zstd, .lz, .lz4, .lzma, .Z, .br                                                                                                                |
| asar     | yes  | no    | .asar                                                                                                                                                                 |
| squashfs | yes  | no    | .sfs, .sqfs, .sqsh, .squashfs, .snap, .appimage                                                                                                                       |


### Creating one

`--pack=FILE` turns the walk around: every match is written into a NEW archive instead of being listed, so the member list is an expression rather than a pipeline into `tar`. The output name picks the format, each member keeps the path it had relative to its search root, and `--sort` decides the order inside. It is a sink like `--summary`, the archive appears only when the walk finished, and a member of another container is refused - harvesting files out of one archive to re-pack them into another is a separate feature, which is also what `-Z++ -z-` is reserved for.

Output filename suffixes this binary writes: `.tar.gz`, `.tar.bz2`, `.tar.xz`, `.tar.zst`, `.tar.lzma`, `.tar.lz4`, `.tar.lz`, `.tar.Z`, `.tbz2`, `.tzst`, `.tbz`, `.tz2`, `.txz`, `.tgz`, `.tlz`, `.taZ`, `.tar`, `.zip`, `.tar.br`, `.tbr`.

`--pack-option=NAME=VALUE|@FILE.json` (repeatable, last value for a NAME wins) tunes the writer. The names are xff's own and are translated for whichever library does the writing, so an unknown one is a usage error and this list is exactly what THIS binary accepts:

The `@FILE.json` form reads one JSON object. Its keys are the option names below; values may be strings, integers, or booleans, with booleans translated to `yes` or `no`. A file is expanded where it occurs among repeated options, so later file or inline values override earlier ones uniformly.

- `compression=store|deflate` - `store` writes members uncompressed, which is what an archive of already-compressed payloads (images, other archives) wants (`zip`)
- `level=N` - how hard the compressor works, on the scale the format uses (also spelled `--pack-level`) (`tar.gz`, `tar.bz2`, `tar.xz`, `tar.zst`, `tar.lzma`, `tar.lz4`, `tar.lz`, `tgz`, `tbz2`, `tbz`, `tz2`, `txz`, `tzst`, `tlz`, `zip`)
- `threads=N` - compressor threads; `0` lets the compressor pick from the machine (`tar.xz`, `tar.zst`, `txz`, `tzst`)
- `timestamp=yes|no` - store the modification time in the gzip header; `no` is what makes two runs over the same input byte-identical (`tar.gz`, `tgz`)
- `zip64=yes|no` - force the zip64 extensions, which lift the 4 GiB member and archive limits (`zip`)
- `framing=rfc9841|raw` - Brotli representation (default `rfc9841`; use `raw` for legacy tools) (`tar.br`, `tbr`)
- `level=0..11` - Brotli quality (default `11`) (`tar.br`, `tbr`)
- `window=10..24` - Brotli LZ77 window bits (default `22`) (`tar.br`, `tbr`)

PHP phars are the exception: xff reads them and can rewrite one to remove members, but it does not CREATE one, because a phar is a PHP program with a stub, a manifest and a signature rather than a container of files. Build one with `box` (box-project/box) or PHP's own `Phar` class, and verify or install one with `phive` (phar-io/phive), which checks the signature xff will not forge.

### Examples

```sh
xff --archive=roots a.tar
```

list the archive and its members

```sh
xff -z+ . -grep TODO
```

search inside every archive met in the tree

```sh
xff --archive=roots a.tgz --summary
```

count what is INSIDE, not the compressed container

```sh
xff --archive=roots --archive-extract a.tar -name '*.json' -exec jq . {} \;
```

run a tool over a member, via a temporary copy

```sh
xff --archive=roots --archive-delete a.tar -name '*.bak' -delete
```

rewrite the archive without those members

```sh
xff . -name '*.cc' -newer VERSION --pack=changed.tar.gz
```

pack what the expression matched into a new archive

See also: [Safety](#topic-safety), [Configuration](#topic-config), [Output](#topic-output)

<a id="topic-stats"></a>

## Statistics

xff statistics reductions. `--summary` and `--histogram` replace the per-match listing with an aggregate over all matches; they are independent and combinable (one walk feeds both), and an explicit action (`-print` / `-exec`) still runs. `--format=jsonl` emits machine rows instead.

### Examples

```sh
xff --summary=ext
```

files + total size per extension

```sh
xff --histogram=ext
```

a bar chart of files per extension

```sh
xff --histogram='ext:sum(lines)'
```

total lines per extension

```sh
xff --histogram=size
```

the file-size distribution

```sh
xff --summary=type --histogram=ext --format=jsonl
```

both, as machine rows

See also: [Comparing trees](#topic-compare), [Fields](#topic-fields), [Output](#topic-output)

<a id="topic-environment"></a>

## Environment

Environment variables xff reads. An explicit command-line flag generally overrides the matching variable.

- `NO_COLOR` - when set (any value), disables color like `--color=never`; `--color=always` still wins (https://no-color.org)
- `XFF_PAGER` - the first automatic environment fallback when neither `less` nor `more` is available
- `PAGER` - the final automatic environment fallback when no known or xff-specific pager is available
- `XFF_MANPAGER` - the pager / formatter for `--man`; overrides the built-in `mandoc` pipeline; set empty to disable
- `COLUMNS` - terminal width used to wrap plain `--help` text for `--width=auto` when the tty size is unknown
- `XDG_CONFIG_HOME` - Git global-ignore discovery root; does not change the xff config location
- `HOME` - Git configuration and global-ignore discovery; does not change the xff config location
- `LC_ALL, LC_CTYPE, LANG` - locale for `--unicode=auto`: a UTF-8 locale selects the Unicode `--format=tree` connectors, else ASCII
- `LSCOLORS` - the same theme in BSD / macOS spelling (11 letter pairs); read when `$LS_COLORS` is unset, which is what makes a themed macOS shell work (see `--color-scheme`)
- `LS_COLORS` - the terminal's colour theme, as `ls` / `dircolors` set it: type keys (`di`, `ln`, `ex`, ...) and per-extension `*.tar=` entries, used by default (see `--color-scheme`)
- `XDG_RUNTIME_DIR` - preferred directory for a member extracted by `--archive-extract`: it is a memory-backed tmpfs, so the copy avoids disk when this location is usable (`/dev/shm` is tried next)
- `TMPDIR` - where a temporary file goes when no memory-backed directory fits it: an extracted member (`--archive-extract`) and the in-progress rewrite of a container (`--archive-delete`)

Any process environment variable is also readable in the field vocabulary as `{env.NAME}` (see `--help=fields`).

See also: [Configuration](#topic-config), [Output](#topic-output)

<a id="topic-cookbook"></a>

## Examples

Worked examples that compose xff's building blocks. Each shows a task, its command, and how it works. See `--help=fields` for the {field}s and `--help=stats` for the reductions.

### Ten largest files

```sh
xff . -type f -printf '%s\t%p\n' | sort -rn | head
```

%s is the size, %p the path; the shell sorts and takes the top ten. -printf builds any columnar line you need.

### Disk use per file type

```sh
xff . -type f --summary=ext
```

a count + total size per extension; the --summary global reads naturally at the end, after the expression (a --long global may sit anywhere). Swap in --histogram=ext for bars, or --histogram='ext:sum(lines)' to rank by lines. See --help=stats.

### Delete stale temp files, safely

```sh
xff . -type f -name '*.tmp' -mtime +7 -delete --dry-run
```

lists what -delete WOULD remove (guarded by --dry-run); rerun without it to delete. -delete implies -depth so directories empty first.

### Search code content, filtered by language

```sh
xff src -lang 'C*' -grep 'TODO'
```

prints every TODO line as path:lineno:text in C / C++ / C# files; add -c for per-file counts or --context=2 for surrounding lines.

### Per-file git-blame author line counts

```sh
xff . -text -exec git blame --line-porcelain {} \; | grep '^author ' | sort | uniq -c | sort -rn
```

runs git blame on each text file; the shell pipe tallies lines per author across the tree. -text skips binaries (which git blame cannot line-blame). -exec feeds any pipeline the field vocabulary cannot express alone.

### Author line counts, natively (no shell pipe)

```sh
xff -g . -text -capturedir:blame git blame --line-porcelain {} \; --summary='{capture.blame:m/^author (.+)$/\1/}'
```

the recipe above with the awk|sort tail folded into xff. -capturedir runs git blame in each file's own directory (repo-safe, works across nested repos); --summary folds that output via an m// extraction, tallying lines per author across the tree - no external pipe. -g honors .gitignore and skips .git; -text keeps blame off binaries. Pass several roots (a b c ...) to span multiple trees. A single-dash global like -g leads; double-dash globals such as --summary may sit anywhere (before or after the paths).

### Checksum manifest for a tree

```sh
xff . -type f -hash:sha256
```

prints `DIGEST  PATH` per file (like sha256sum); redirect to a file to snapshot a tree, then diff two runs to spot changes.

### Create a patch between repository trees

```sh
xff -g+ --compare=diff old-tree new-tree > changes.patch
```

walks both trees using the requested traversal and ignore options, then writes one unified patch for added, removed, and changed text files, including files found only on the right - unlike a one-sided `-diff TARGET` walk. Binary differences are reported in the patch stream. Use --compare-select to restrict which result kinds contribute.

### Recently changed files as machine rows

```sh
xff . -type f -mtime -1 --format=jsonl
```

everything modified in the last day, one JSON object per file, ready for jq or a script.

See also: [Configuration](#topic-config), [Expression](#topic-expressions), [Output](#topic-output), [Safety](#topic-safety)

## Exit status

0 on success, 2 on error. With `--quiet` or `--exit-match` the exit is 0 when something matched and 1 when nothing did (an error still outranks the match status).

## See also

find(1), grep(1), fnmatch(3), glob(7), pcre2pattern(3)

For the `--regextype` grammars see the Regex grammars section above (`--help=grammars`). FNMATCH is the platform's fnmatch(3) and PCRE2 is pcre2pattern(3); GLOB and SHGLOB are xff's path-aware globs (compiled to RE2), NOT POSIX glob(7) - that page is listed only as background on shell globbing. The default RE2 grammar has no man page; its syntax is at https://github.com/google/re2/wiki/Syntax .
