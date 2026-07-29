# Copilot / AI agent instructions

This repository's canonical guidance for AI coding agents lives in
[`AGENTS.md`](../AGENTS.md) at the repo root. **Read it first.**

It covers what the project is, the repository layout, the core architecture reading order,
build/test commands (including fast single-test iteration), coding conventions, CI behavior,
and a "do not" list.

Additional references:

- [`CLAUDE.md`](../CLAUDE.md) — Claude Code entry point (imports `AGENTS.md`)
- [`.claude/skills/`](../.claude/skills/) — on-demand Claude Code skills: `openusd-coding-style`,
  `create-hvt-task`, `commit-content`
- [`README.md`](../README.md) — user-facing build instructions
- [`docs/README.md`](../docs/README.md) — subsystem design docs and reading order
  (includes [`docs/build.md`](../docs/build.md) and [`docs/testing.md`](../docs/testing.md))
- [`test/README.md`](../test/README.md) — how-to integration guide
