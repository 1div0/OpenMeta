Security
========

OpenMeta assumes metadata comes from untrusted files. The main risks are memory
corruption, resource exhaustion (including decompression bombs), and output
injection (terminals, JSON/XML, logs).

What the code does
------------------

- Bounds-checks every offset/size before reading.
- Guards arithmetic when computing sizes (overflow checks + range validation).
- Enforces explicit decode limits (entries/IFDs/value bytes).
- Sanitizes console output (ASCII-only, escapes control/non-ASCII, truncates).
- Caps decompression output size and part counts.

Safe/unsafe contract
--------------------

- Safe paths are default: they return explicit status/issues for malformed or
  unsafe metadata text and do not silently fall back to raw bytes.
- Unsafe paths are explicit opt-in and may expose raw metadata bytes, while
  still enforcing memory/path/resource safety checks.
- Safe console rendering uses structured placeholders for unsafe text (for
  example ``<CORRUPTED_TEXT:...>``) to avoid display/log injection confusion.

Validation issue codes
----------------------

``metavalidate`` exposes machine-readable issue codes that can be consumed by
gates and CI (for example ``xmp/output_truncated`` and
``xmp/invalid_or_malformed_xml_text``).

What the tests do
-----------------

- Unit tests cover normal and malformed inputs.
- libFuzzer and FuzzTest targets exercise parsers under sanitizers.

For the full policy and threat model, see ``SECURITY.md`` in the repository.
