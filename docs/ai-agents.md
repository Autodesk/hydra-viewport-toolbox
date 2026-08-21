# AI agent guidance — design

HVT ships optional guidance for AI coding agents (Cursor, Claude Code, GitHub Copilot, and
similar tools). **No agent or specific editor is required** to build, test, or contribute — see
[README.md](../README.md).

This document explains **why** the layout looks the way it does: one shared guide, one shared
skills directory, and thin per-tool entry points — instead of duplicating rules for every editor.

## Goals

1. **Single source of truth** — conventions and workflows live in one place, not forked per tool.
2. **Skills over scoped rules** — deep, task-specific guidance loads on demand instead of many
   file-pattern rules that drift out of sync.
3. **Tool-agnostic core** — [`AGENTS.md`](../AGENTS.md) and [`.claude/skills/`](../.claude/skills/)
   are usable by any agent that can read markdown; editor-specific files stay minimal.
4. **Low maintenance** — adding a skill or updating a convention should not require editing four
   parallel rule files.

## Layer model

Guidance is split into three layers by **when** it should load:

| Layer | Location | When it loads | Contents |
|-------|----------|---------------|----------|
| **Always-on** | [`AGENTS.md`](../AGENTS.md) | Every agent session | Repo purpose, layout, architecture reading order, baseline conventions, common tasks, "do not" list |
| **On-demand skills** | [`.claude/skills/<name>/SKILL.md`](../.claude/skills/) | When the task matches the skill description | Deep workflows: C++ style, adding a task, commits/PRs |
| **Reference docs** | [`docs/build.md`](build.md), [`docs/testing.md`](testing.md), feature docs | When build/test/feature depth is needed | Presets, vcpkg, test filtering, subsystem design |

```text
Every session
    └── AGENTS.md  (always)
            ├── task matches? → read .claude/skills/<name>/SKILL.md
            └── need depth?   → follow links to docs/build.md, docs/testing.md, …
```

Baseline conventions (license headers, `HVT_API`, Hydra task shape, test expectations) stay in
`AGENTS.md` so they are always present. Skills hold the **how-to** detail that would bloat the
always-on guide or belong only to certain tasks.

## Why skills live under `.claude/skills/`

The directory name reflects where the format originated (Claude Code project skills), but the
files are **not Claude-specific**:

- Each skill is a plain `SKILL.md` with YAML frontmatter (`name`, `description`) and a markdown body.
- Descriptions define **when** an agent should read the file — the same trigger logic works in
  Cursor, Claude Code, or any tool that can open a path on demand.
- Content is HVT-specific (OpenUSD/Hydra conventions), not Claude API or prompt syntax.

We deliberately **do not** copy skills into `.cursor/skills/` or maintain a second tree. One
directory, one edit, all agents see the same guidance.

## Per-tool entry points

Each editor gets a thin adapter — not a copy of the conventions:

| Tool | Entry file | Role |
|------|------------|------|
| **Any agent** | [`AGENTS.md`](../AGENTS.md) | Canonical guide at repo root |
| **Claude Code** | [`CLAUDE.md`](../CLAUDE.md) | Single line: `@AGENTS.md` |
| **Cursor** | [`.cursor/rules/agents-guide.mdc`](../.cursor/rules/agents-guide.mdc) | `alwaysApply: true`, imports `@AGENTS.md`, lists skills to read from `.claude/skills/` |
| **GitHub Copilot** | [`.github/copilot-instructions.md`](../.github/copilot-instructions.md) | Points to `AGENTS.md` and `.claude/skills/` |

Cursor does **not** use native project skills (`.cursor/skills/`) in this repo. The always-applied
rule tells the agent to **read** matching files from `.claude/skills/` when the task fits. That
avoids symlinks, Windows symlink quirks, and duplicated skill maintenance.

## Cursor indexing vs access (`.cursorignore` / `.cursorindexingignore`)

Two files control what Cursor agents see:

| File | Effect |
|------|--------|
| [`.cursorignore`](../.cursorignore) | **Blocks read and edit** — agent cannot open these paths at all. Only `externals/vcpkg/` (never edited from this repo). |
| [`.cursorindexingignore`](../.cursorindexingignore) | **Excluded from semantic index** but still **readable on demand** — `build/`, `install/`, `out/`, `test/data/baselines/`, and `**/*.png`. Keeps OpenUSD headers under `build/<preset>/vcpkg_installed/.../include/pxr/` openable when debugging Hydra API usage. |

Golden-image baselines and build trees stay out of the index to save context, but agents can still
open them when fixing image-comparison failures or inspecting vcpkg-installed headers.

## What we removed (and why)

An earlier setup used several Cursor **scoped rules** (architecture, CMake, C++/Hydra patterns,
testing) as `.cursor/rules/*.mdc` with file globs. That was replaced by:

- **Always-on basics** → `AGENTS.md`
- **Build / test depth** → [`docs/build.md`](build.md), [`docs/testing.md`](testing.md)
- **Task-specific depth** → `.claude/skills/`

Scoped rules tended to duplicate `AGENTS.md` and docs, and had to be updated in multiple places
when conventions changed. The skills-first model keeps one authoritative file per concern.

## Current skills

| Skill | Read when |
|-------|-----------|
| [`openusd-coding-style`](../.claude/skills/openusd-coding-style/SKILL.md) | Writing or editing C++ (`.h`/`.cpp`/`.glslfx`) under `include/hvt/` or `source/` |
| [`create-hvt-task`](../.claude/skills/create-hvt-task/SKILL.md) | Adding a new `HdxTask` subclass, shaders, TaskManager wiring, or task tests |
| [`commit-content`](../.claude/skills/commit-content/SKILL.md) | Writing commit messages or opening PRs |

Skills may reference each other (e.g. `create-hvt-task` points to `openusd-coding-style` for
headers and formatting).

## Adding a new skill

1. Create `.claude/skills/<skill-name>/SKILL.md` with frontmatter:

   ```yaml
   ---
   name: skill-name
   description: >-
     What the skill does. Include trigger terms so agents know WHEN to read it.
   ---
   ```

2. Keep the body focused and actionable (checklists, file paths, examples).
3. Add a one-line mention in `AGENTS.md` → Conventions (skills list) if it is a general
   contributor workflow.
4. Add a bullet under **On-demand skills** in
   [`.cursor/rules/agents-guide.mdc`](../.cursor/rules/agents-guide.mdc) so Cursor agents know
   to read it.
5. Optionally extend [`.github/copilot-instructions.md`](../.github/copilot-instructions.md).

Do **not** duplicate the skill under `.cursor/skills/` unless the project explicitly adopts
native Cursor skill discovery later.

## Trade-offs

| Choice | Benefit | Cost |
|--------|---------|------|
| Shared `.claude/skills/` | One source of truth, works across tools | Directory name suggests Claude-only (mitigated by this doc) |
| Cursor rule-only bridge (no symlink) | Simple, no Windows symlink issues | Skills are read on demand, not auto-invoked from Cursor's Skills panel |
| No scoped `.mdc` rules | Less duplication | Agents must follow links to `docs/` for build/test detail instead of glob-triggered rules |
| `AGENTS.md` always applied | Stable baseline every session | Grows if too much detail is pushed into the always-on layer — keep deep content in skills/docs |

## Related files

- [`AGENTS.md`](../AGENTS.md) — start here
- [`CONTRIBUTING.md`](../CONTRIBUTING.md) — human contributor process (CLA, PR targets)
- [`docs/README.md`](README.md) — subsystem design index
