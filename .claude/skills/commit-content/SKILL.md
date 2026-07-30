---
name: commit-content
description: >-
  HVT git commit and PR conventions. Use when creating a git commit or preparing
  a pull request in this repo — commit message shape (subject/body/footers),
  the pre-commit checklist (clang-format, license headers, tests), and PR rules
  (CLA, branch from main, target main). Read before writing a commit message.
---

# HVT commit & PR conventions

These conventions are observed in the project's git history and CONTRIBUTING.md. Follow them when
committing or opening a PR.

## Commit message

```
<Imperative subject, optionally TICKET-123 prefix>

<Prose body: what changed and WHY, wrapped ~72 cols. Explain rationale, not a
line-by-line of the diff.>

<footers>
```

- **Subject:** imperative mood ("Add…", "Fix…", "Drop…"), ~50–72 chars, no trailing period is
  fine either way (both appear in history). An issue key prefix is optional and common
  (`AGP-517 - …`, `KBSH-1269 - …`). Squash-merge PR subjects also carry a `(#123)`
  suffix — that is added by the merge, don't hand-write it on local commits.
- **Body:** blank line after the subject, then a short prose paragraph on the reason for the
  change. Skip only for truly trivial commits.
- **Footers (when applicable):**
  - `Signed-off-by: Name <email>` — DCO-style sign-off, used on the large majority of commits.
  - `Co-authored-by: Name <email>` — for pair/AI-assisted work (e.g. `Cursor`, or the Claude
    agent per its standing footer convention).

## Before you commit

1. Run `clang-format` on changed C++ files (see the `openusd-coding-style` skill).
2. Every **new** file has the Apache-2.0 license header with the current year.
3. Include tests for behavior changes; add/update **one** How-to if a user-facing feature changed.
4. Don't commit generated files (`include/hvt/namespace.h`), build output, or anything under
   `externals/vcpkg/`.

## Pull requests (from CONTRIBUTING.md)

- A signed **Contributor License Agreement (CLA)** is required before PRs can be accepted.
- **Branch from the latest `main`** and open the PR **targeting `main`** — sync with
  `origin/main` before you start, and set the PR base branch to `main`.
- Fill in the PR template (`.github/PULL_REQUEST_TEMPLATE.md`): description, changes, test
  configuration/results, documentation, and the checklist (including the CLA item).
