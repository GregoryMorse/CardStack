# Migration fixture corpus

The unified tests validate the content, size, SHA-256 and provenance contracts in corpus-manifest.json. There are 61 artifacts and 50 semantic cases: 50 newly witnessed original-UI artifacts, three original samples, two historical exports and six explicitly synthetic boundaries.

The 33 original-application authoring recipes cover all fourteen stock deck templates and their 45 report presets, a saved/reopened forty-field custom deck, both owner-cipher schedules, password rejection/acceptance, a genuine add/encrypt/reopen/remove/reopen cycle, long variable notes, maximum field input, multiple data pages and DBF/DBT, CRD, WP and TN exports with notes. Schema, field values, whitespace, layouts and report properties are compared, not just record counts. All six declared completeness gates passed before promotion.

The historical notes_heavy, max_lengths, many_fields, oem_text, unusual_index and truncated files are synthetic or byte-patched controls. They do not prove original UI boundary acceptance. security_cycle.BTN is now a genuinely witnessed cycle, not the former plain-file placeholder. blocked_cipher_previous.BTN is a valid historical blocked-schedule example, not malformed input.

Notes descriptor length forty is a minimum reservation, not a maximum. The original 8191-character edit saves/reopens 8190 in the witnessed boundary case. The reader also has independent full-8191 unit coverage. Deck/report strings use Windows-1252; explicit DOS-codec tests and original OEM dBASE exports remain distinct.

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/validate_winevdm_golden_fixtures.ps1 -BuildDir build/vs2026-local-qt -Configuration Release -RequirePending
~~~

The validator executes CardStackTests directly so a supplied FixtureDir cannot be overwritten by CTest configuration. Required semantic methods must execute without skips. Five negative controls prove that wrong content, null native-run provenance, wrong hashes, missing files and unlisted artifacts are rejected. A hash-only run cannot certify completeness.

Regenerate into a fresh disposable directory with collect_winevdm_corpus.ps1. Complete validation and promotion require every enabled native recipe. All semantic checks must pass before the manifest is published last. generation-evidence.json retains hashes and message-only run summaries without depending on private source paths.

These are migration data contracts, not a promise that every original printing behavior or every older visual-layout variant is implemented. Connected report-field flow, original print-coordinate conversion and WINDEX label/control/font migration have focused regressions. The non-Software mailing_layout.TN fixture retains both original text labels and all ten controls after a native save/reopen. Complete corpus validation requires successful, non-null native-run summaries and matching artifact provenance.
