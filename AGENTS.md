# AGENTS.md

Guidance for future agents working in this repository.

## Project Shape

nanoev is a small C async event library. Keep the public API compact and the
implementation explicit. The core loop is single-threaded; events belong to
their loop and, except for documented cross-thread APIs such as
`nanoev_async_send()`, should be operated from the loop thread.

Supported platforms are Windows, macOS, and Linux. Platform code lives behind
small internal abstractions in `source/nanoev_internal_*.c` and the poller
implementations. Preserve that boundary when adding features.

## Branches And Commits

- Use plain branch names without a `codex` prefix.
- Use conventional commit messages, for example `feat: ...`,
  `fix: ...`, `refactor: ...`, or `docs: ...`.
- Keep commits focused. Separate API additions from follow-up refactors when
  both are meaningful.
- Before creating a PR, push the branch and write a PR description covering
  what changed, why, behavior notes, and validation.

## Build And Test

After code changes, run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Also run:

```sh
git diff --check
```

If CMake files changed, make sure the existing `build` directory reconfigures
cleanly. Keep generated build artifacts out of commits.

## API Design

- The public API is not stable yet. Do not assume backward compatibility is
  required for every change.
- When an API change could be either additive or breaking, ask whether
  compatibility matters for that task before choosing the shape.
- If compatibility is requested, prefer additive APIs over changing existing
  public function signatures.
- If compatibility is not required, keep the API simple even when that means
  changing an existing function signature.
- For larger option sets, consider an `_ex` API or an options struct, but do
  not add that complexity unless it is useful for the current design.
- Do not hide potentially blocking work inside APIs that look like normal event
  loop operations.
- Document callback threading, object lifetimes, pending-operation limits, and
  cancellation or timeout semantics in `include/nanoev.h`.
- Keep `include/nanoev.hpp` as a thin C++ include wrapper.

## Threading

Use the internal thread/mutex/condition wrappers instead of direct pthread or
Win32 calls outside the platform abstraction layer. Keep thread lifecycle
management explicit and join worker threads during termination.

The DNS subsystem uses `dns_init()` / `dns_term()` internally. Prefer this
subsystem naming style (`xxx_init()` / `xxx_term()`) for future process-wide
internal services. If a subsystem owns worker threads, shut it down before
platform cleanup that those workers may depend on.

## Cross-Platform Care

- Windows socket and resolver APIs require `ws2_32`.
- Non-Windows thread use should go through CMake `Threads::Threads`.
- Be careful with Windows IOCP and overlapped completion semantics when adding
  cancellation, timeout, or close-on-pending behavior.
- Keep Unix/macOS behavior behind the existing poller and internal abstraction
  boundaries.

## Review Checklist

Before finishing:

- Build and test locally.
- Check whitespace with `git diff --check`.
- Review public header comments for new API behavior.
- Update README usage notes for user-visible behavior changes.
- Confirm `git status --short --branch` is clean after committing or merging.
