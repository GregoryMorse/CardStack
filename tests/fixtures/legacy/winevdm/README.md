# WineVDM legacy golden fixtures

This directory is the canonical test fixture copy of the legacy card database
golden corpus. The source/provenance copy is generated or staged through:

`build\winevdm-oracle\fixtures`

The files here are intentionally committed under `tests/fixtures` so legacy
import tests run by default without requiring environment variables. Regenerate
or update fixtures in the generated oracle output directory first, validate
them, then promote the accepted files here.

Current required files:

- `plain.BTN`
- `pwonly.BTN`
- `crypt.BTN`
- `reports.BTN`
- `reports.RPT`
- `notes_heavy.BTN`
- `many_fields.BTN`
- `max_lengths.BTN`
- `security_cycle.BTN`
- `EXDBF.DBF`
- `EXDBF.DBT`
- `EXCRD.CRD`
- `EXWP.WP`
- `EXTN.TN`
- `PWTN.TN`

`scripts/validate_winevdm_golden_fixtures.ps1` validates this directory by
default.
