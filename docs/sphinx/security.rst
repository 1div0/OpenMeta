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

What the tests do
-----------------

- Unit tests cover normal and malformed inputs.
- libFuzzer and FuzzTest targets exercise parsers under sanitizers.

For the full policy and threat model, see ``SECURITY.md`` in the repository.

