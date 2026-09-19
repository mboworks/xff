<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Inspecting configuration

Run the intended command with `--explain` before relying on a profile. It discovers enabled config
files, validates them, resolves selectors, and prints the resulting configuration without evaluating
the expression. Invalid selected profiles, unreadable existing files, and missing explicit files
remain errors; explain mode does not bypass them.

Read the output in this order:

1. **Sources:** which files were consulted and whether they existed. This is discovery order;
   the application trace below determines when their flags take effect.
2. **Profile declarations:** each file's named sections, their source line, selection, availability,
   and declared contribution kinds (`globals`, `predicates`, `actions`, or `empty`). Refinements
   of one name appear separately for each file. `skipped` means a config skip request excluded its
   named sections. `disabled` means validation rejected that name; the original diagnostic and
   composition failures are retained. A disabled declaration also prevents selecting that name's
   refinements from other files. `reserved-preset` identifies an attempted built-in style override.
3. **Application trace:** flags in the actual resolution order, with file/line/section markers.
   CLI selectors compose at their requested positions. Repeated selections remain visible here.
4. **Safety decisions and dropped directives:** the effective blocks, safe profile, scope roots,
   and rejected directives explain what the configuration can authorize. A profile being
   available or selected does not arm its actions. Mandatory blocks cannot be undone by a later
   permissive setting.

Unselected invalid sections can be inspected with `xff --explain`. Selecting one remains a hard
error. The declaration list describes available configuration, not proof that every declared action
will execute or that arbitrary child processes are sandboxed.

## Personal defaults

Place these lines in the fixed user config at the OS account home directory's `.config/xff/config`:

```ini
--color=auto
[reports]
--summary=ext
```

```sh
xff . --config=reports --explain
```

`reports` is available and selected, declaring globals. The unsectioned color setting applies without
a selector. `--no-user-config` excludes named sections; existing user globals still apply unless the
trusted require-globals policy permits skipping them.

## An explicit project profile

Save this as `project.xffrc`:

```ini
[sources]
-type f -name '*.cc'
```

```sh
xff . --xffrc=project.xffrc --config=sources --explain
```

The named section declares predicates. Loading this file does not select all its sections. A profile
containing execution or deletion actions would still require trusted authorization; an rc file cannot
arm itself. Automatic `.xffrc` discovery is separately opt-in through `--rc` or `--rc+`.

## Mandatory system policy

In `/etc/xff.ini`:

```ini
--require-system-globals
--block-execution
--block-file-deletion
```

```sh
xff . --no-system-config --no-safe --explain
```

The globals remain required and both unconditional blocks remain effective. The skip request excludes
named system sections; disabling the safe profile does not remove unconditional blocks. Requiring
system globals is also the default when the directive is omitted.

## An environment-derived output root

In the user config's unsectioned part:

```ini
--block-policy-categories=output
--output-root="${HOME:?HOME must be set}/xff-results"
```

```sh
xff . --explain
```

The safety section shows the expanded output root and its source. `${NAME:-DEFAULT}` supplies a
fallback for an unset or empty variable; `${NAME:?MESSAGE}` fails the config line instead. The
replacement stays within one argument and is not executed or parsed as extra flags. This does not
make an environment-controlled path trustworthy: an administrator who needs a fixed protection
boundary should use a fixed path under their control. Declaring a root categorizes operations;
it does not create the directory or grant a capability by itself.

See [the configuration model](design-config.md) for composition and trust rules, and
[the safety model](design-safety.md) for capability enforcement.
