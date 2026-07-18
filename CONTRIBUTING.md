# Contributing to KaSON

Thank you for improving KaSON. Keep changes focused, allocation behavior
explicit, and public API changes documented.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Before submitting parser or schema changes, run the sanitizer configuration:

```sh
cmake -S . -B build-asan \
  -DKaSON_ENABLE_SANITIZERS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

Changes affecting parsing should add focused unit tests and, when relevant, JSON
conformance or fuzz-smoke coverage. Changes affecting schema packing or
unpacking should cover both success and failure behavior.

## Documentation

Public behavior belongs in one of these places:

- `README.md`: project selection, first build, and navigation
- `docs/tutorial.md`: task-oriented learning
- `docs/api-guide.md`: contracts, ownership, and API relationships
- Public headers: exact symbol-level reference
- `CHANGELOG.md`: user-visible release changes

Install Doxygen, then run:

```sh
./docs/check.sh
```

This validates repository-local Markdown links and builds Doxygen with warnings
treated as errors. Keep example source files authoritative; tutorial excerpts
should remain short and must agree with the compiled programs under `examples/`.

## Style and compatibility

- Preserve C99 compatibility.
- Match the surrounding C formatting and naming conventions.
- Do not introduce heap allocation into the parser or schema implementation.
- Document ownership, lifetime, capacity, and error behavior for new APIs.
- Update `docs/compatibility.md` and `CHANGELOG.md` for compatibility-affecting
  changes.

Run `git diff --check` before submitting. Continuous integration builds with GCC
and Clang, runs the test suite with sanitizers, and verifies the documentation.
