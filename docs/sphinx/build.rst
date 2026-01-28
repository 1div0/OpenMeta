Build and Install
=================

OpenMeta uses CMake and has no required third-party dependencies for the core
read path.

Build
-----

.. code-block:: bash

   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build

Install
-------

.. code-block:: bash

   cmake --install build --prefix /opt/openmeta

Options
-------

Core toggles:

- ``OPENMETA_BUILD_STATIC`` / ``OPENMETA_BUILD_SHARED``: build static/shared OpenMeta libraries.
- ``OPENMETA_BUILD_TOOLS``: build the ``metaread`` CLI.
- ``OPENMETA_BUILD_TESTS``: build GoogleTest unit tests.
- ``OPENMETA_BUILD_FUZZERS``: build libFuzzer targets (Clang).
- ``OPENMETA_BUILD_FUZZTEST``: build FuzzTest-based fuzz targets (when available).
- ``OPENMETA_FUZZTEST_FUZZING_MODE``: enable FuzzTest fuzzing-mode flags.
- ``OPENMETA_WITH_ZLIB`` / ``OPENMETA_WITH_BROTLI``: enable payload decompression
  when the system libraries are available.
- ``OPENMETA_WITH_EXPAT``: enable XMP packet parsing when Expat is available.
- ``OPENMETA_USE_LIBCXX``: build against ``libc++`` (useful when deps were built with ``libc++``).

Docs (optional):

- ``OPENMETA_BUILD_DOCS``: generate Doxygen HTML on ``install``.
- ``OPENMETA_BUILD_SPHINX_DOCS``: generate a Sphinx site (Doxygen XML + Breathe).
- ``OPENMETA_PYTHON_EXECUTABLE``: override the Python interpreter used for Sphinx
  (useful for uv/venv/conda when CMake would otherwise pick system Python).

Python (optional):

- ``OPENMETA_BUILD_PYTHON``: build Python bindings (nanobind).
- ``OPENMETA_BUILD_WHEEL``: add an ``openmeta_wheel`` build target and copy the wheel on ``install``.
- ``OPENMETA_WHEEL_NO_BUILD_ISOLATION``: use ``pip wheel --no-build-isolation`` during wheel builds.

If you install dependencies into a custom prefix, provide it via
``CMAKE_PREFIX_PATH``.

.. code-block:: bash

   cmake -S . -B build -DCMAKE_PREFIX_PATH=/mnt/f/UBS
