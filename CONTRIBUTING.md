# Contributing to OpenMeta

Thanks for contributing to OpenMeta.

OpenMeta is a C++ metadata library with a strong focus on safe parsing,
deterministic behavior, and regression-gated format support. This file explains
how to ask questions, report problems, and submit changes.

## Communication

GitHub is the main project communication channel for OpenMeta.

- Questions, bug reports, feature requests, and design discussions should go to
  GitHub Issues:
  https://github.com/ssh4net/OpenMeta/issues
- Code changes should go through GitHub Pull Requests:
  https://github.com/ssh4net/OpenMeta/pulls

For larger changes, open an issue first so the scope and direction are clear
before you spend time implementing them.

## Security Reports

Do not open public GitHub issues for suspected vulnerabilities.

Use GitHub private vulnerability reporting instead:

- https://github.com/ssh4net/OpenMeta/security/advisories/new

See also [SECURITY.md](SECURITY.md).

## Before You Start

- Check for an existing issue or pull request before opening a new one.
- Keep each change focused on one topic.
- Split unrelated refactors from behavior changes.
- Update docs when you change public APIs, CLI behavior, or user-visible
  output.

## Reporting Bugs

When filing a bug, include enough detail for somebody else to reproduce it.
Useful details include:

- OpenMeta version
- Platform and compiler
- Build flags or unusual local configuration
- The input file or a minimal reproducer
- What you expected to happen
- What actually happened

If the problem is in parsing or export behavior, a small sample file or hex
snippet is often more useful than a long description.

## Project Layout

Main public paths:

- `src/include/openmeta/`: public headers
- `src/openmeta/`: core implementation
- `src/tools/`: CLI tools
- `src/python/`: Python bindings and helper scripts
- `tests/`: unit tests, smoke tests, fuzz targets
- `registry/`: public, human-reviewed metadata registry
- `docs/`: public documentation

## Build and Test

Core library:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Unit tests:

```bash
cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DOPENMETA_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

If your test dependencies live in a custom prefix, pass
`-DCMAKE_PREFIX_PATH=/path/to/deps`.

libFuzzer targets:

```bash
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DOPENMETA_BUILD_FUZZERS=ON
cmake --build build-fuzz
ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_container_scan -max_total_time=60
```

Run the relevant build and test steps for your change before opening a pull
request. Parser, decoder, validation, and export changes should include tests,
and should run fuzz targets when practical.

## Coding Style

OpenMeta follows a low-abstraction C++20 style.

- No exceptions
- No RTTI
- Deterministic behavior
- Explicit ownership
- No lambdas in OpenMeta sources
- Match the existing structure and naming style
- Format with `.clang-format`

Prefer small, reviewable patches over broad cleanup. Do not reformat unrelated
files just because you are touching nearby code.

New C and C++ source files should begin with:

```cpp
// SPDX-License-Identifier: Apache-2.0
```

## Tests and Regression Coverage

- Add or update tests for functional changes.
- Keep changes regression-gated where possible.
- If you fix a bug, add coverage for that case.
- If you change CLI output or validation behavior, update the relevant smoke
  tests or documentation.

See [docs/development.md](docs/development.md) and
[docs/sphinx/testing.rst](docs/sphinx/testing.rst) for more detail.

## Commit Sign-Off

OpenMeta uses Developer Certificate of Origin style sign-off for
contributions.

Each commit should include a `Signed-off-by` line, for example:

```text
Signed-off-by: Your Name <you@example.com>
```

The simplest way to add this is:

```bash
git commit -s
```

or:

```bash
git commit --signoff
```

By signing off a commit, you confirm that you have the right to submit the
change under the project's Apache 2.0 license. If you are contributing as part
of your job, make sure your employer allows that contribution.

## Pull Requests

- Base changes on `main`.
- Use a short, imperative commit subject.
- Sign off each commit with `Signed-off-by`.
- Describe what changed, why it changed, and any limitations.
- Include build and test notes in the pull request description.
- Keep the pull request focused. If a change needs background discussion, link
  the GitHub issue.

Draft pull requests are fine when you want early feedback on direction or API
shape.

## Licensing

OpenMeta is licensed under Apache 2.0. Contributions submitted for inclusion in
OpenMeta must be made under that license.
