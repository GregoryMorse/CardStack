# Import and document-processing safety

CardStack rejects malformed or over-budget input without publishing a partial imported deck. Existing destination files are not replaced when validation or a save budget fails. Import source files are read-only.

## Default limits

The named defaults and configurable policy are in `src/core/ImportBudget.h`.

| Resource | Default |
| --- | --- |
| Input file | 64 MiB |
| Charged decoded data and metadata | 128 MiB |
| Rows and values | 1,000,000 each |
| Fields | 1,024 |
| Design frames | 65,536 |
| Individual decoded value | 8 MiB |
| Native Btrieve record | 1 MiB |
| Work units | 512 Mi units |
| Processing deadline | 10 seconds |
| Snapshot columns per table | 128 |
| Snapshot JSON nesting | 128 levels |

Budgets are conservative processing limits, not promised maximum document sizes or exact process-RSS measurements. Metadata, intermediate records, padded cells and final model values can each consume budget. SQLite cell projection uses a conservative expansion allowance. A document can therefore reach its decoded-data limit before its input-size limit. SQLite database and journal sizes are checked together.

The migration APIs accept lower `ImportLimits` for embedding applications and tests. A shared budget spans Btrieve preflight, native extraction, model mapping and call-log sidecars. Cancellation, deadline and budget failures are terminal; they do not enter heuristic recovery or TakeNote fallback. Fingerprint hashing is chunked and bounded too.

## Important guarantees and boundaries

- Cardfile offsets are unsigned and range-checked before access. Truncated indexes and text payloads are errors, not silently truncated cards.
- dBase record spans and memo blocks use checked wide arithmetic.
- Decrypted Btrieve input is passed to the native reader in memory, never through a plaintext temporary database. The owned input buffer is wiped on exit; this does not promise that every Qt or model allocation is wiped from RAM.
- Native progress callbacks run inside page and fragment processing. The host also limits retained records and decoded output.
- SQLite import accepts ordinary tables, rejects views, virtual/shadow tables and generated columns, limits projected cells, and validates snapshot structure and typed values before changing target rows.
- Authenticated encrypted snapshots receive lexical size, string, node and nesting checks before JSON construction, followed by full snapshot validation.
- Existing password verification, authentication, read-only handling and atomic replacement checks remain enabled.

The cooperative deadline cannot interrupt a blocked operating-system file read or a single SQLite engine call. Qt, SQLite and the operating system are separate dependencies, not fully instrumented or exhaustively proven by these tests. Large legitimate documents may require a future explicit product policy rather than silently removing the limits.

## Automated regressions

`ImportSecurityTests` covers exact CRD offset boundaries, truncated payloads, dBase/memo arithmetic, processing budgets, cancellation, native file/memory parity, shared-fragment amplification, null variable pointers, encrypted migration, fallback suppression, atomic call-log import, report frame limits, SQLite schema/typed-value rejection, failed-save preservation, JSON limits and bounded fingerprinting. It also runs 48 deterministic mutations in isolated, time-limited child processes. This is a reproducible mutation corpus, not exhaustive coverage-guided fuzzing.

`BtrieveMemoryInputTests` is the dependency's standalone C harness. It is included in CardStack's normal CTest run and can also be compiled independently using the dependency's `memory-input.md` instructions.

Run the normal tests with the installed Qt runtime available:

```sh
ctest --test-dir build --output-on-failure
```

For a separate Clang/GCC instrumented build:

```sh
cmake -S . -B build-sanitized -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCARDSTACK_ENABLE_SANITIZERS=ON -DCARDSTACK_DEPLOY_QT_AFTER_BUILD=OFF
cmake --build build-sanitized --target CardStackTests
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-sanitized --output-on-failure \
  -R 'ImportSecurityTests|Legacy.*ReaderTests|Btrieve.*Tests|SQLite.*StoreTests|DelimitedTextTests|PhoneCallLogTests'
```

On MSVC, the same CMake option enables AddressSanitizer with debug symbols; UndefinedBehaviorSanitizer is exercised by the Linux Clang CI job. The ASan runtime must be on `PATH`, normally by running from the matching x64 Visual Studio developer prompt. Tests remain headless and do not use the physical mouse or keyboard.
