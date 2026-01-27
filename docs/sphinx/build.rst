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

- ``OPENMETA_BUILD_TOOLS``: build the ``metaread`` CLI.
- ``OPENMETA_WITH_ZLIB`` / ``OPENMETA_WITH_BROTLI``: enable payload decompression
  when the system libraries are available.

Docs (optional):

- ``OPENMETA_BUILD_DOCS``: generate Doxygen HTML on ``install``.
- ``OPENMETA_BUILD_SPHINX_DOCS``: generate a Sphinx site (Doxygen XML + Breathe).

If you install dependencies into a custom prefix, provide it via
``CMAKE_PREFIX_PATH``.

.. code-block:: bash

   cmake -S . -B build -DCMAKE_PREFIX_PATH=/mnt/f/UBS

