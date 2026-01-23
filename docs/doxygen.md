# Doxygen

OpenMeta keeps documentation lightweight and close to the public headers.

## Requirements

- `doxygen` (optional: `graphviz` for diagrams)

## Generate API docs

From the `OpenMeta/` repo root:

```bash
doxygen Doxyfile
```

Output goes to `build/docs/html/index.html`.

## What gets documented

- Markdown: `README.md` is used as the main page.
- Public API: everything under `include/openmeta/`.

If you add new public headers or APIs, prefer documenting at the header or
type/function level so the docs stay accurate as code moves.
