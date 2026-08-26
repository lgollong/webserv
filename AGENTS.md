# AGENTS.md

Instructions for any AI coding agent working in this repository.

## Hard Requirements

- This is a 42 `webserv` project implemented in C++98.
- Do not use external dependencies or Boost.
- The project must compile with `c++ -Wall -Wextra -Werror -std=c++98`.
- Keep the server single-process and event-loop driven.
- Socket and pipe I/O must be non-blocking and readiness-driven through one `poll()` or equivalent event loop.
- Do not call `read`, `recv`, `write`, or `send` on sockets or pipes without prior readiness from the event loop.
- Do not inspect `errno` after `read`, `recv`, `write`, or `send`.
- Regular disk files may be read or written without readiness polling.
- Use `fork` only for CGI.
- The server must not crash or terminate unexpectedly.
- Every implemented behavior should align with the project subject in `docs/subject/en.subject.md`.

## Git

Use Conventional Commits for all commit messages.

Format:

```text
<type>(optional-scope): <description>
```

Common types:

- `feat`
- `fix`
- `refactor`
- `test`
- `docs`
- `chore`
- `build`
- `ci`
- `perf`

Examples:

```text
feat(config): parse server blocks
fix(worker): avoid errno checks after socket writes
docs(readme): add build and run instructions
test(http): cover malformed request lines
```

## Workflow

Before implementing any change:

1. Get the newest version of `main`.
2. Create a GitHub issue for the work.
3. Keep the issue focused on one feature, fix, or documentation task.
4. Include the relevant subject requirement in the issue.
5. Add clear acceptance criteria.
6. Add the expected tests or manual verification steps.
7. Create a new branch from `main` for that issue only.

Issue template:

```md
## Subject Requirement

Quote or paraphrase the exact requirement this issue addresses.

## Goal

Describe the behavior that should exist when this issue is done.

## Acceptance Criteria

- [ ] Compiles with `-Wall -Wextra -Werror -std=c++98`
- [ ] Covered by a manual or scripted test
- [ ] Does not violate the non-blocking socket/pipe I/O rule
- [ ] Works with a provided config example when relevant

## Notes

Mention relevant files, constraints, or design decisions.
```

While implementing:

- Keep changes small and focused on the issue.
- Do not mix unrelated cleanup with feature work.
- Preserve existing user changes.
- Prefer the project's existing structure and naming patterns.
- Add tests or manual verification notes for behavior changes.

Before finishing:

- Run `make`.
- Run any relevant tests or manual checks.
- Confirm the change maps back to the issue's subject requirement.
- Summarize what changed and how it was verified.

Before merging:

- Update documentation when behavior, architecture, workflow, or subject coverage changes.
- Keep `docs/architecture.md` aligned with the implemented code.
- Keep issue acceptance criteria and subject-compliance notes current.
- Do not merge changes that make docs describe behavior the code no longer has.
