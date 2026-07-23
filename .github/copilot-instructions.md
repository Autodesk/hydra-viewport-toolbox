# Copilot / AI agent instructions

This repository's canonical guidance for AI coding agents lives in
[`AGENTS.md`](../AGENTS.md) at the repo root. **Read it first.**

It covers what the project is, the repository layout, the core architecture reading order,
build/test commands (including fast single-test iteration), coding conventions, CI behavior,
and a "do not" list.

Additional references:

- [`CLAUDE.md`](../CLAUDE.md) — Claude Code entry point (imports `AGENTS.md`)
- [`.claude/rules/`](../.claude/rules/) — path-scoped rules for Claude Code (import `.cursor/rules/`)
- [`README.md`](../README.md) — user-facing build instructions
- [`docs/README.md`](../docs/README.md) — subsystem design docs and reading order
- [`test/README.md`](../test/README.md) — how-to integration guide
- [`.cursor/rules/`](../.cursor/rules/) — scoped rules for architecture, CMake, C++/Hydra
  patterns, and testing (also useful as context for non-Cursor agents)
