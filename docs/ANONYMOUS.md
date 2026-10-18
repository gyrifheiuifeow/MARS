# Anonymous submission copy

The submission copy contains the current source files and recorded query results,
without Git history or remote configuration. Personal account names, workstation
paths and server hostnames have been removed from the accompanying records.
Historical paths now use `/workspace/` as a placeholder. Third-party copyright
notices and upstream references are retained.

The original historical archive attachments are not part of this copy. Their
contents and archive headers can contain author information. They must be
anonymized separately before distribution to reviewers. The original repository,
its releases, commit pages and old tags must not be used as submission links.

Prepared datasets must be supplied through the submission's own data distribution.
The extraction helper accepts local archive files with `--assets-dir`, or an
explicit mirror with `--base-url`. It contains no built-in repository address and
does not read account credentials. Use `--manifest` with the checksum manifest of
the supplied files; re-created anonymous archives have different checksums.

Path redaction changes some metadata files, but does not change query IDs,
returned objects, scores, timings, or aggregate numerical results. The two
delivery inventories have been updated for the available files. Historical
hashes for external source files and indexes remain historical references.
`verify_sources.py`, `verify_raw_results.py` and `verify_results.py` check the
source and numerical records included here.

The submission ZIP has no `.git` directory and uses neutral ZIP timestamps and
permissions. `SUBMISSION_SHA256.json` records its file hashes. A clean scan for
known identifiers does not make a personal repository URL or its old history
anonymous.
