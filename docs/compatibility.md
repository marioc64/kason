# Compatibility policy

[Documentation home](index.md) · [Tutorial](tutorial.md) ·
[API guide](api-guide.md)

## Language and build requirements

- The library implementation and public headers target C99.
- Public headers provide `extern "C"` guards for use from C++.
- The supported CMake entry point requires CMake 3.16 or newer.
- GCC and Clang are exercised by continuous integration with warnings,
  AddressSanitizer, and UndefinedBehaviorSanitizer enabled.

Platform-specific defects are welcome as bug reports when they can be
reproduced with a conforming C99 implementation.

## Versioning

KaSON follows semantic-versioning intent. While the project remains below
version 1.0, a minor release may change source or binary compatibility. Patch
releases should remain source compatible and focus on fixes.

Every user-visible API or behavior change should be recorded in
[`CHANGELOG.md`](../CHANGELOG.md). Applications that require a stable ABI should
pin an exact pre-1.0 minor version and rebuild dependents when upgrading.

## Compatibility surface

The supported source interface consists of documented declarations in
`kason.h` and `kason_schema.h`. Structure fields described as internal parser
state are exposed only to permit caller-owned allocation and are not a stable
interface.

Installed CMake consumers should link the exported `kason::kason` or
`kason::schema` targets instead of depending on library filenames or installation
layout.
