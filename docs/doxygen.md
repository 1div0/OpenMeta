# Documentation

OpenMeta uses Doxygen for API extraction. For the "site" style docs (like OIIO),
we render Doxygen XML via Sphinx + Breathe.

## Requirements

- `doxygen` (optional: `graphviz` for diagrams)
- For Sphinx docs: Python packages listed in `docs/requirements.txt`

## Generate API docs (CMake)

Enable docs and build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENMETA_BUILD_DOCS=ON
cmake --build build --target openmeta_docs
```

Output goes to `build/docs/doxygen/html/index.html` inside the build directory.

When `OPENMETA_BUILD_DOCS=ON`, docs are also generated during `install`:

```bash
cmake --build build --target install
```

## Generate site docs (Sphinx)

Enable Sphinx docs and build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENMETA_BUILD_SPHINX_DOCS=ON
cmake --build build --target openmeta_docs_sphinx
```

Output goes to `build/docs/html/index.html`. Doxygen XML goes to
`build/docs/doxygen/xml/index.xml`.

## Publish on GitHub Pages

The repository includes a GitHub Actions workflow at
`.github/workflows/docs-pages.yml` that builds the Sphinx site and publishes it
to GitHub Pages.

Recommended setup:

- In the repository settings, set **Pages** to **GitHub Actions** as the
  source. This is a one-time repository setting.
- Pull requests build the docs but do not deploy.
- Pushes to `main` and tag pushes build and deploy the site.

The workflow only runs when docs inputs change: `docs/**`, `src/include/**`,
`README.md`, `SECURITY.md`, `NOTICE.md`, `CMakeLists.txt`, or the workflow
itself.

## Generate API docs (manual)

From the `OpenMeta/` repo root:

```bash
doxygen docs/Doxyfile
```

Output goes to `build/docs/doxygen/html/index.html`.

## What gets documented

- Markdown: `README.md` is used as the main page.
- Public API: everything under `src/include/openmeta/`.

If you add new public headers or APIs, prefer documenting at the header or
type/function level so the docs stay accurate as code moves.
