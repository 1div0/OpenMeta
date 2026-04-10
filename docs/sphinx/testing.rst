Testing
=======

OpenMeta treats metadata parsing as security-sensitive code. Tests and fuzzing
are part of the expected workflow for parser changes.

Unit tests (GoogleTest)
-----------------------

.. code-block:: bash

   cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug \
     -DCMAKE_PREFIX_PATH=/mnt/f/UBSd -DOPENMETA_BUILD_TESTS=ON
   cmake --build build-tests
   ctest --test-dir build-tests --output-on-failure

If your test dependencies were built against ``libc++`` (common with Clang),
configure OpenMeta with ``-DOPENMETA_USE_LIBCXX=ON``.

Fuzzing
-------

libFuzzer targets (Clang):

.. code-block:: bash

   cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPENMETA_BUILD_FUZZERS=ON
   cmake --build build-fuzz
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_exif_tiff_decode -max_total_time=60

Corpus runs
~~~~~~~~~~~

If you pass corpus directories to libFuzzer, it treats the **first** directory
as the main corpus and may add/reduce files there. To avoid modifying your seed
corpus, use an empty output directory first:

.. code-block:: bash

   mkdir -p build-fuzz/_corpus_out
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_container_scan \
     build-fuzz/_corpus_out \
     /path/to/seed-corpus-a /path/to/seed-corpus-b \
     -runs=1000

Public in-tree seed corpus:

.. code-block:: bash

   mkdir -p build-fuzz/_corpus_out
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_container_scan \
     build-fuzz/_corpus_out \
     tests/fuzz/corpus/container_scan \
     -runs=1000

The ``container_scan`` seed set includes BMFF ``iloc`` method-2 relation
cases (valid v1 ``iref`` mapping, missing mapping, out-of-range
``extent_index``, and ``idx_size=0`` reference mismatch).

FuzzTest targets (when available):

.. code-block:: bash

   cmake -S . -B build-fuzztest -G Ninja -DCMAKE_BUILD_TYPE=Debug \
     -DCMAKE_PREFIX_PATH=/mnt/f/UBSd -DOPENMETA_BUILD_FUZZTEST=ON -DOPENMETA_FUZZTEST_FUZZING_MODE=ON
   cmake --build build-fuzztest
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzztest/openmeta_fuzztest_metastore --fuzz_for=10s

CLI smoke gates
---------------

Public-tree smoke targets (self-contained, no external corpus required):

.. code-block:: bash

   cmake --build build-tests --target openmeta_gate_metavalidate_smoke
   ctest --test-dir build-tests -R openmeta_cli_metavalidate_smoke --output-on-failure

.. code-block:: bash

   cmake --build build-tests --target openmeta_gate_metaread_safe_text_smoke

These gates provide fast regression checks for safe-output and validation
behavior. Corpus-scale compare/baseline gates are expected to run in project CI
or release validation workflows.

Transfer release gate
---------------------

The stronger transfer release gate rolls up the main transfer-focused unit
suite and the public transfer smoke coverage into one named check.

- In a non-Python test tree it runs:

  - ``MetadataTransferApi.*``
  - ``XmpDump.*``
  - ``ExrAdapter.*``
  - ``DngSdkAdapter.*``
  - ``openmeta_cli_metatransfer_smoke``

- In a Python-enabled test tree it also runs:

  - ``openmeta_python_transfer_probe_smoke``
  - ``openmeta_python_metatransfer_edit_smoke``

.. code-block:: bash

   cmake --build build-tests --target openmeta_gate_transfer_release
   ctest --test-dir build-tests -R openmeta_transfer_release_gate --output-on-failure

The public GitHub Actions workflow ``.github/workflows/ci.yml`` runs two Linux
variants of these public release gates:

- self-contained non-Python, non-DNG-SDK
- Python-enabled, non-DNG-SDK, with ``nanobind`` installed into the CI
  interpreter via ``pip``

Read release gate
-----------------

The read release gate rolls up the main self-contained decode, scan, and
interop-adapter suites into one named check. It includes coverage such as:

- ``ContainerScan.*``
- ``ContainerPayload.*``
- ``ExifTiffDecode.*``
- ``SimpleMetaRead.*``
- ``XmpDecodeTest.*``
- ``JumbfDecode.*``
- ``OiioAdapter.*``
- ``OcioAdapter.*``
- ``ValidateFile.*``

.. code-block:: bash

   cmake --build build-tests --target openmeta_gate_read_release
   ctest --test-dir build-tests -R openmeta_read_release_gate --output-on-failure

CLI release gate
----------------

The CLI release gate rolls up the self-contained public CLI smokes that are
not already part of the transfer gate:

- ``openmeta_cli_metaread_safe_text_smoke``
- ``openmeta_cli_metavalidate_smoke``
- ``openmeta_cli_numeric_parse_smoke``

.. code-block:: bash

   cmake --build build-tests --target openmeta_gate_cli_release
   ctest --test-dir build-tests -R openmeta_cli_release_gate --output-on-failure

Interop adapter tests
---------------------

Adapter-focused tests in the public tree:

.. code-block:: bash

   cmake --build build-tests --target openmeta_tests
   ./build-tests/openmeta_tests --gtest_filter='InteropExport.*:OiioAdapter.*:OcioAdapter.*'
   ./build-tests/openmeta_tests --gtest_filter='CrwCiffDecode.*'

These tests cover:

- alias/spec name-policy behavior in ``InteropExport``,
- OIIO/OCIO adapter export stability,
- CRW/CIFF derived EXIF mapping for legacy Canon RAW.
