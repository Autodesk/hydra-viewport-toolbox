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

1. Run `clang-format` on the **changed lines** of changed C++ files — not whole files, which would
   sweep in pre-existing formatting drift (see the `openusd-coding-style` skill → Formatting scope).
2. Every **new** file has the Apache-2.0 license header with the current year.
3. Include tests for behavior changes; add/update **one** How-to if a user-facing feature changed.
4. Update what references the change — see "Counterpart map" below.
5. Don't commit generated files (`include/hvt/namespace.h`), build output, or anything under
   `externals/vcpkg/`.

## Counterpart map

When a change lands, everything that describes it must land with it. If the change matches a row
but you didn't update that counterpart, say why in the commit body. Map:

| Change | Counterparts |
|--------|--------------|
| Public API (`include/hvt/`) | `docs/README.md` index; dedicated `docs/<feature>.md` if behavior or the recommended entry point changed; `AGENTS.md` pointers |
| New task | `test/README.md` How-to list, `docs/README.md` task tables (see the `create-hvt-task` skill) |
| New How-to | `test/README.md` How-to list, the How-to table in `docs/README.md` |
| New `docs/*.md` | `docs/README.md` index |
| New skill | `AGENTS.md` skills list, `.cursor/rules/agents-guide.mdc`, `docs/ai-agents.md` (optionally `.github/copilot-instructions.md`, per *Adding a new skill*) |
| New CMake option or env var | `docs/build.md` (build options / env var tables) |
| Feature behavior | The feature's `docs/<feature>.md` and its How-to |
| Convention (style, review, commit, task structure) | The `.claude/skills/` file that encodes it |
| Test baselines | Reason in the PR description (see the `review-hvt-pr` skill) |

## Pull requests (from CONTRIBUTING.md)

- A signed **Contributor License Agreement (CLA)** is required before PRs can be accepted.
- **Branch from the latest `main`** and open the PR **targeting `main`** — sync with
  `origin/main` before you start, and set the PR base branch to `main`.
- **Branch naming:** `<username>/<short-name>` (e.g. `jdoe/my-feature`), where
  `<username>` is the GitHub login of the author. The prefix keeps branch names on the shared
  remote collision-free and makes ownership obvious. When an agent creates the branch on the
  user's behalf (e.g. a "commit and push to PR" request), derive the username from
  `gh api user --jq .login` and use a short, descriptive name — not the commit subject.
- **Pre-push check:** before pushing a non-`main` branch, make sure the lean preset check runs
  (`.githooks/pre-push` — builds plus How-to tests, not the full suite; see `docs/git-hooks.md`).
  If `git config core.hooksPath` returns
  `.githooks`, the hook fires automatically — just push, and do **not** also run the script
  manually (that doubles the check). Otherwise install it once with
  `git config core.hooksPath .githooks` and push; if writing git config is not possible
  (e.g. a sandboxed shell), run `.githooks/pre-push` directly instead and abort the push when
  it exits non-zero — unless the failure is environmental (sandbox, network, or permission
  errors rather than compile or test failures), in which case request the needed permissions
  and retry, or defer to CI and say so.
- Fill in the PR template (`.github/PULL_REQUEST_TEMPLATE.md`): description, changes, test
  configuration/results, documentation, and the checklist (including the CLA item).
