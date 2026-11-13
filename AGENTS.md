---
description: SatEngine C++20 coding conventions for headers and sources
globs: "**/*.{hpp,h,cpp,cc,cxx}"
---

# C++ Standards

Target C++20. Match existing SatEngine style. Keep APIs small, explicit, and const-correct. Prefer clear code and enforceable rules over unnecessary abstraction.

IMPORTANT: Make the code as optimize as possible. As performance as possible.

## Naming

- Namespace: `sat`.
- Types and enums: `PascalCase`.
- Functions, methods, parameters, and local variables: `camelCase`.
- Non-static data members: `m_` prefix.
- Named constants: `kPascalCase`.
- Macros: `SAT_UPPER_SNAKE`; use only when necessary.
- Prefer `constexpr`, typed constants, and `enum class` over macros.
- Files: `PascalCase`, matching the primary type where applicable (`Window.hpp` / `Window.cpp`).

## Formatting

- Match the existing code formatting in files you edit and in new files you create: indentation, brace style, spacing, line breaks, include layout, and naming as already used nearby.
- For new files, copy the style of the closest existing file in the same area (same folder or sibling module), not a personal or default agent style.
- Prefer the surrounding / sibling file's style over any personal or default agent style. Do not reformat unrelated code or impose a new convention.
- Follow the repository formatter configuration when one exists; otherwise copy the local pattern in the touched or sibling file.
- Use braces for control-flow bodies.
- Keep functions focused; extract helpers when they clarify intent, isolate an invariant, or remove duplication.
- Avoid unrelated formatting changes and drive-by refactors.

## Headers and Includes

- Use `#pragma once` in project headers.
- Make headers self-contained: include the declarations and definitions required to compile each header independently.
- Include what you use directly; do not rely on transitive includes.
- Include order: own header → project → third-party → standard library.
- Use quotes for project headers and angle brackets for third-party and standard-library headers.
- Forward-declare types when sufficient; include complete definitions when required.
- Do not manually forward-declare standard-library types.
- Do not put `using namespace` directives in headers.
- Keep implementation details and platform dependencies out of public interfaces where practical.
- Use PIMPL when dependency isolation or interface stability justifies its allocation and indirection.
- Avoid non-inline function or variable definitions in headers unless required for templates or otherwise permitted by the language.

## Ownership and Lifetime

- Use RAII for memory, files, locks, operating-system handles, and GPU resources.
- Prefer values and standard containers before dynamic ownership.
- Use `std::unique_ptr` for exclusive dynamic ownership.
- Use `std::shared_ptr` only when ownership is genuinely shared; prevent ownership cycles.
- Prefer `std::make_unique` and `std::make_shared` where appropriate.
- Raw pointers and references normally represent non-owning access; document exceptions.
- Use references for required objects and pointers when absence or reseating is meaningful.
- Document ownership transfers, borrowed lifetimes, and invalidation rules.
- Do not return references, pointers, spans, or views to destroyed or invalidated storage.
- Avoid explicit `new` and `delete` outside resource-management implementations.

## Initialization and Special Members

- Initialize every member before use; prefer default member initializers for defaults.
- Prefer the Rule of Zero.
- For resource-owning types, explicitly decide whether copying and moving are supported.
- Delete unsupported copy or move operations.
- When declaring a destructor or copy/move operation, review all special member functions.
- Mark move operations `noexcept` when their implementations guarantee it.
- For PIMPL with `std::unique_ptr<Impl>`, define destruction and operations requiring a complete `Impl` out of line after its definition.
- Avoid unintended narrowing conversions and uninitialized scalar values.

## Language and API Design

- Mark constructors callable with one argument `explicit` unless implicit conversion is intentional.
- Prefer `enum class` over unscoped enums.
- Use `nullptr` instead of `NULL` or `0` for null pointers.
- Mark observers `const`; prefer immutable local values where practical.
- Use `[[nodiscard]]` for results whose accidental omission is likely a bug.
- Pass small, cheap types by value and larger read-only objects by `const&`.
- Use `std::string_view` and `std::span` for borrowed strings and contiguous ranges when appropriate.
- Do not assume `std::string_view` is null-terminated.
- Make ownership and lifetime requirements explicit when storing borrowed arguments.
- Use `std::optional` for an absent value; preserve error details when failure needs explanation.
- Use `auto` when it improves clarity or avoids repetition; keep meaningful types explicit.
- Prefer named C++ casts over C-style casts; isolate and justify unsafe casts.
- Use `override` for overrides and `final` only when preventing extension is intentional.
- A base class must have a public virtual destructor or a protected non-virtual destructor when applicable.
- Prefer composition when inheritance does not model a clear interface relationship.
- Class access-section order: `public` → `protected` → `private`.
- Do not use language or library features newer than C++20 without an approved compatibility layer.

## Error Handling and Contracts

- Follow the subsystem's established exception or result-type policy.
- If no policy exists, establish one before introducing a new public error-handling convention.
- Report initialization failures with actionable context; never silently discard failures.
- Use assertions for programmer errors and internal invariants, not recoverable runtime failures.
- Validate external input and runtime failures in release builds.
- Do not allow exceptions to escape destructors or cross incompatible C/plugin boundaries.
- Use `noexcept` only when the operation guarantees that no exception escapes.
- Preserve valid object state and release acquired resources when an operation fails.
- Prefer fully initialized objects.
- If explicit `init()` / `shutdown()` is required, define valid states, partial-failure cleanup, repeated-call behavior, and destructor responsibilities.
- Do not use `std::expected` directly; it requires C++23.

## Threading

- Document thread affinity and whether APIs support concurrent access.
- Synchronize shared mutable state; `const` alone does not imply thread safety.
- Prefer scoped locking and RAII thread ownership.
- Do not invoke unknown callbacks while holding a lock unless the contract explicitly requires it.
- Define callback, task, and captured-object lifetimes.
- Ensure shutdown cancels or completes outstanding work before destroying its dependencies.
- Use atomics with a documented synchronization purpose; do not treat `volatile` as thread synchronization.

## Engine Conventions

- Keep platform-specific behavior behind appropriate subsystem boundaries.
- Document coordinate systems, handedness, units, angle conventions, and matrix layout at API boundaries.
- Define CPU/GPU resource ownership, thread affinity, synchronization, and destruction timing.
- Define handle validity, invalidation, and behavior after resource destruction.
- Avoid unnecessary heap allocation, blocking operations, and synchronization in update/render hot paths.
- Use profiling to justify performance-sensitive complexity.
- Use virtual dispatch where appropriate; avoid unnecessary dispatch in measured hot paths.
- Keep logging bounded and appropriate for per-frame code.
- Comment invariants, lifetime requirements, threading assumptions, and non-obvious decisions.

## Portability and Data

- Follow the repository's supported compiler and platform matrix.
- Use fixed-width integer types where exact widths matter, such as binary formats and protocols.
- Handle signed/unsigned conversions, overflow, and size conversions deliberately.
- Do not serialize objects by dumping their memory representation.
- Specify byte order, field sizes, alignment requirements, and format versioning for binary data.
- Isolate compiler extensions and platform APIs; provide portable interfaces where required.

## Validation

- Follow repository formatting, warning, and static-analysis configuration.
- Keep changed code free of new warnings; do not suppress diagnostics without a specific justification.
- Build affected targets and run checks appropriate to the change.
- Add or update tests for meaningful behavior changes and bug fixes.
- Use available sanitizers for relevant memory, undefined-behavior, and concurrency checks.
- Keep changes focused on the requested problem.

# Register New Files in CMakeLists

When adding or moving any engine/game source, update the matching `CMakeLists.txt` in the **same change**. Skipping this leaves the file out of the CMake target, so IntelliSense/clangd report errors and the build ignores the file.

## Checklist (do every time)

1. Add every new `.cpp` to the correct target source list.
2. If you add a new **public** `include/` directory, add it to `target_include_directories(... PUBLIC ...)`.
3. If you add a new `src/module/` tree (or need `"module/Foo.hpp"` includes), ensure the parent `src/` is on `target_include_directories(... PRIVATE ...)` (Renderer already has `Renderer/src`).
4. Do not rely on globbing — list sources explicitly (project convention).

## Module folder (`src/module/`)

Use `Engine/<Area>/src/module/` for focused implementation units that are **not** public API (e.g. `MeshPass`, `SelectionOutlinePass`, `TranslateGizmoPass`).

- Keep each module’s `.hpp` next to its `.cpp` under `src/module/` — do **not** put these headers in `include/`
- Third-party / graphics API types (D3D11, etc.) may appear only in `src/` and `src/module/`
- The area facade (e.g. `Renderer`) owns orchestration and includes modules as `"module/Foo.hpp"`
- Other engine areas follow the same pattern when they grow internal multi-file subsystems