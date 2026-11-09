# Submission files

The tracked source and result records use `/workspace/` in place of personal
machine paths. Third-party copyright notices retain the upstream authors' names.

For review, use a file export that excludes `.git` and remote configuration.
Historical archive attachments contain machine details in their contents and
headers, so they need separate redaction. Check the exported files and the
anonymous site's rendered pages before sharing the link.

Supply datasets through the submission's data distribution. The extraction
helper accepts local archives with `--assets-dir`, or a mirror with `--base-url`.
Pass the supplied files' checksum manifest with `--manifest`. Repacking archives
changes their hashes even if the vector files inside them stay the same.

`verify_sources.py` checks the published source files. `verify_raw_results.py`
checks the per-query CSVs and `verify_results.py` recomputes summary statistics.
Historical source and index hashes describe their saved versions. The delivery
inventories cover the available local files after path redaction.

The previously exported submission ZIP uses neutral timestamps and permissions.
Its `SUBMISSION_SHA256.json` lists file hashes for checking the export.
