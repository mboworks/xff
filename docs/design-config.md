# xff - Configuration System Design

> Detailed spec for the layered config system. **Refines** `design.md`
> §"Security & safety (review #2)" and §"Flag / toggle conventions"; where the
> two differ, this document is authoritative for config specifically.
> Status: **Draft** · 2026-06-21 · Org: MBO Works

> **SUPERSEDED IN PART - the project layer was dropped (Option B, 2026-07-06).**
> xff has **no auto-discovered project config**: no per-directory `.xffrc`, no ancestor
> cascade, no subtree scoping, and no `--project-config` flag. Config comes from **three
> tiers only**: **system** (`/etc/xff.ini`, root-owned), **user**
> (`~/.config/xff/config`), and an **explicit `--xffrc=FILE`** (naming the file is the
> consent to load it). Per-directory _ignore_ rules (`.gitignore` / `.xffignore`) are an
> ignore concern, not config, and are unaffected. Every mention below of the "project"
> layer, the `.xffrc` cascade, or `--project-config` is historical - read it as removed.
> The `--xffrc` arming restriction (a named file cannot self-arm `-exec`/`-delete`;
> `--allow-exec` arms from the CLI/user/system tiers only) is the remaining live rule; see
> `TODO.md` "Config: drop the project `.xffrc` layer".

## Purpose

A find-compatible tool that can run `-exec`/`-capture` is, by construction, a
code-execution engine. A config system for it must make the common case
convenient **without** ever letting an untrusted, repo-local file silently run
commands. The design goal, in the user's words: _"by default there is no
security issue."_

The system has **four layers** with a strict trust order, and a single
root-owned **policy** that decides how much each lower (less-trusted) layer may
do. The policy is the keystone: absence of an admin policy yields the _safe_
default, never an unsafe one.

## The four layers

Lowest → highest precedence; each overrides values set by an earlier one, and
the **CLI always wins** (typing a flag is explicit consent).

| #   | Layer       | Owner / trust            | Format                  |
| --- | ----------- | ------------------------ | ----------------------- |
| 1   | built-in    | the binary               | (none)                  |
| 2   | **system**  | root - the trust anchor  | INI (structured policy) |
| 3   | **user**    | the user - trusted as-is | `.xffrc` grammar        |
| 4   | **project** | repo - **untrusted**     | `.xffrc` grammar        |
| 5   | **CLI**     | explicit user consent    | flags                   |

(Locations are detailed under Discovery below.)

- **System** holds global defaults _and_ the per-flag **policy** (below). Only
  this root-owned layer may grant capability; lower layers can never widen their
  own permissions.
- **User** is trusted as the user themselves - data and armed `[exec]`/named
  blocks both honoured.
- **Project** `.xffrc` cascades like `.gitignore` (each directory's file scopes
  to its subtree; cannot add roots, redirect output, or reach outside its
  subtree - per `design.md` §132) and is **capped by the policy**: sensitive
  flags are inert unless the system policy grants the project layer.
- Provenance: every setting records `enum{unset, system, user, project, cli}`
  (not `bool`), so resolution is last-non-unset-wins and _unset_ ≠ _off_
  (`design.md` §102).

## Discovery & loading

- **System:** `/etc/xff.ini` then `/etc/xff.d/*.ini` (lexical). Never skipped by
  `--no-config` (a security control, not a convenience). Absent ⇒ built-in
  default policy applies.
- **User:** `$XFF_CONFIG` if set, else `$XDG_CONFIG_HOME/xff/config`, else
  `~/.config/xff/config`.
- **Project:** auto-discovered - from each search root, every `.xffrc` from the
  filesystem root down to the entry's directory contributes to that subtree
  (cascade). Ownership-gated even for data-only settings (`design.md` §132).
- **Explicit:** `--xffrc=FILE` loads a specific file and **arms** its
  sensitive/named blocks (ownership-gated). Naming the file _is_ the
  authorization - no trust DB, no hashes. This replaces the working
  `--config <file>` spelling in `design.md` §130 (which now collides with
  `--config=NAME` below).
- **`--no-config`:** consult no config file at all, including the system, user,
  and explicitly named `--xffrc` paths; pure CLI + built-ins. With no
  config-sourced directive to gate, loading the system policy would have no
  security effect. Like Bazel's `--ignore_all_rc_files`, this is absolute
  regardless of other config-related option ordering.

## Formats

### System policy - `/etc/xff.ini` (structured)

INI: a `[defaults]` section of plain flag lines, and a `[policy]` section of
per-layer, per-flag `allow`/`deny` lists. Structured because a policy wants
typed validation and must be unambiguous.

```ini
[defaults]
--color = auto

[policy]
# Per-flag overrides on top of the built-in safety classification.
# Built-in default already denies @sensitive/@destructive at the project layer.
project.allow = --sort, --color, --format       # redundant with built-ins; explicit is fine
project.deny  = --threads                        # tighten: forbid even a "safe" flag
user.allow    = @sensitive                       # user may arm -exec/-capture from ~/.config
```

- Class tokens `@safe` / `@sensitive` / `@destructive` expand to every flag with
  that registry `safety` classification (`design.md` §134), so a _new_
  sensitive flag is covered without editing the policy - **no gaps as the tool
  grows.**
- `deny` beats `allow` on conflict. A layer can be tightened (deny a safe flag)
  or loosened (allow a sensitive one) - only by this root-owned file.

### `.xffrc` (user & project) - bazel-rc grammar

Line-oriented flag bundles ("what you can type, you can save", `design.md`
§125), each optionally prefixed by a **two-axis selector** `base:config:`:

```
# <base>:<config>:  <flags…>     base ∈ {common, <named-config>, ∅(=common)}
common:        --color=auto                 # every run, always (your defaults)
myproj:        --feature=long-paths         # only when --config=myproj is active
xff:myproj:    --feature=trace --threads=1  # only when the xff style AND --config=myproj
```

- `base` is `common`/empty (applies always - your standing defaults) or a **named
  config** (applies only when that `--config=NAME` is active). A **bare built-in-style
  base** (`xff:` / `find:` / `rg:` with no `:config`) is **rejected**: a config
  file may not attach behavior to a preset, so a plain `xff` run stays reproducible (the
  gate drops such a line and warns). `:config` gates by an active named config; a
  `style:config:` prefix additionally scopes it to a style (it still needs the named
  config active, so it never fires on a plain preset run). Customize by defining a named
  config and activating it with `--config=NAME` or an argv[0] alias of that name.
- A line is inert (parsed to AST, never executed). Sensitive flags within it are
  subject to the policy of the _layer the file belongs to_.

## Capability policy (per-flag, safe-by-default)

The user-chosen model is **per-flag allow/deny**, anchored to the registry's
built-in `safety` classification so the safe default needs no admin file:

| Class         | Project | User  | System |
| ------------- | ------- | ----- | ------ |
| `safe`        | allow   | allow | allow  |
| `sensitive`   | deny    | allow | allow  |
| `destructive` | deny    | allow | allow  |

`safe` = output / traversal / matching (`--color`, `--sort`, `-name`); `sensitive` = the exec family (`-exec`, `-execdir`, `-ok`, `-okdir`, `-capture`, `-capturedir`); `destructive` = `-delete` and future built-in mutators.

- **No `/etc/xff.ini` ⇒ this built-in table is the policy.** A repo-local
  `.xffrc` can recolor output or set a default sort; it can **not** run a
  command or delete a file. That is "no security issue by default".
- The system policy's `[policy]` lists **override** this table per flag/class
  per layer (loosen or tighten). Lower layers cannot edit policy.
- Class membership is intrinsic to each descriptor, so newly added sensitive
  flags inherit the deny without a policy edit.

### Enforcement & self-documentation

- A disallowed setting from a config layer is **dropped, not fatal** (a hostile
  `.xffrc` must never abort or DoS the run), with a self-documenting stderr note
  - never silent (`design.md` §134):

  ```
  xff: ignoring `-exec` from ./.xffrc - denied for the project layer [security];
       arm it explicitly with `--xffrc=./.xffrc`, or allow in /etc/xff.ini.
  ```

- `--explain` lists every config source consulted, what each contributed, and
  every gate that fired with its _why_.
- Interacts with `--safe` (`design.md` §136): `--safe` is an orthogonal,
  user-facing hard-refuse of destructive/dangerous ops regardless of layer; the
  policy governs _where settings may originate_. Both can refuse; both explain.

## CLI selectors

- **`--config=NAME`** - repeatable, **stacks** (bazel-style; later wins on
  conflict, explicit flags beat config-expanded ones). `NAME` ∈ the built-in
  styles `find` / `xff` **plus** any `:NAME` block from loaded `.xffrc` files.
  Selecting a built-in style is how you pick the base behaviour - this
  **subsumes a separate `--style` flag.** Version-extensible: `--config=xff:2`
  pins a behavioural epoch (the part that scales to "even more modern", unlike a
  binary `--modern`).
- **`--feature=NAME` / `--feature=no-NAME`** - repeatable on/off capability
  gates; the granular dial within a style (a style is just a named bundle of
  feature defaults). Valued options (`--implicit-print=no`) stay dedicated flags,
  **not** `--feature`. (The `--capture-override` that used to be the other example
  here is gone: a name-reuse opt-in belongs on the NODE that reuses the name,
  `-capture:!NAME`, not on a flag that loosens every node in the command.)
- **`--xffrc=FILE`** - load + arm a specific file (see Discovery).
- **`--no-config`** - see Discovery.
- **`--project-config=on|warn|off`** - a per-directory (project) `.xffrc` lives in a
  tree the user may not control, so it is **off unless explicitly enabled**: `on`
  applies it (still safe-subset only - sensitive/destructive lines and **style
  selectors** are never honored from a project file, so a repo cannot relax the
  find baseline or arm `-exec`); `warn` (**default**) ignores it but prints one
  stderr note when a project `.xffrc` was found; `off` ignores it silently. Full
  config, including style relaxation, works only from the user and system layers.
- **`argv[0]` dispatch** - the invocation name is the leading `--config` selector.
  A built-in style name selects that preset: `find` (strict: accept only find's own
  primaries/options, reject every xff extension incl. single-dash modern primitives
  like `-println`), `xff` (modern), `rg` (the single opinionated flavor). **Any other
  name selects a same-named _named config_** (there is no `xfd`/`fd` magic - they are
  just names): a `mytool` symlink to `xff` activates the user/system `mytool:` block
  while the base style stays the modern `xff` default - the sanctioned way to ship a
  personal preset without overloading a built-in one. An explicit `--config=` still
  stacks over it. (Subsumes the standalone argv[0]-dispatch task.)

## Worked examples

```sh
# Hostile repo → a per-directory .xffrc is OFF by default: nothing from ./.xffrc applies.
cd ./untrusted && xff .
#   ./.xffrc present → ignored; "a per-directory .xffrc was found but ignored" on stderr
#   (silence with --project-config=off)

# Opt in to a per-directory .xffrc. Even then it is safe-subset only: sensitive/destructive
# lines and style selectors are never honored from a project file (those need user/system config).
cd ./trusted && xff --project-config=on .
#   ./.xffrc: common: --color=never     → applied (safe)
#   ./.xffrc: common: -exec rm {} ;      → dropped + warned (sensitive, project-denied)

# Personal exec recipe from your own (user) config → trusted, applied with no opt-in.
#   ~/.config/xff/config: xff:thumbs: -capture:W=… convert {} … ;
xff --config=thumbs ~/Pictures            # armed: user layer allows @sensitive

# CI host loosens the project layer for a vetted pipeline (opt-in + policy allow).
#   /etc/xff.ini: [policy] project.allow = -capture
cd $CI_WORKSPACE && xff --project-config=on .   # repo .xffrc -capture now armed

# Strict drop-in via name.
ln -s xff find && ./find . -println        # → error: -println unknown (find style)
```

## Implementation roadmap (small PRs)

- **A** [#58]: this spec (reviewable design).
- **B** [#58]: config loader skeleton - parse INI + `.xffrc` grammar, layer precedence, provenance enums, `--no-config`, `--xffrc=FILE`, `--config=NAME` selection (no gating yet).
- **C** [#58]: policy gate - registry `safety` classification, built-in default table, `[policy]` overrides, drop-and-warn enforcement, **hostile-`.xffrc` rejection tests**.
- **D** [#54, #59]: wire `--config=find|xff` styles + `--feature`, tag every descriptor find-vs-xff, strict-style filter + conformance.
- **E** [#59]: `argv[0]` default style; cascading project discovery; `--explain` config trace.

## Open questions

- `/etc/xff.d/` drop-in dir now, or single `/etc/xff.ini` first?
- Class tokens (`@sensitive`) in the `[policy]` lists from day one, or per-flag
  names only until proven necessary?
- Windows: system path (`%PROGRAMDATA%\xff`) and user path mapping (deferred with
  the rest of Windows support - `design.md` §Non-Goals).

## Prior art

- **ripgrep:** config only via an explicit `RIPGREP_CONFIG_PATH` env var - no
  ambient discovery. (We add cascading project files, but gate them.)
- **fd:** ships _no_ config file at all - the maximally safe stance.
- **sudoers / ssh:** a root-owned system file defines policy that user files
  cannot override - the model for our system-over-project trust anchor.
- **bazel:** `--config=NAME` named configs and rc layering - adopted, minus
  bazel's workspace auto-load (the exact hole we must not reproduce).
