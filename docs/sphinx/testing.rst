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

FuzzTest targets (when available):

.. code-block:: bash

   cmake -S . -B build-fuzztest -G Ninja -DCMAKE_BUILD_TYPE=Debug \
     -DCMAKE_PREFIX_PATH=/mnt/f/UBSd -DOPENMETA_BUILD_FUZZTEST=ON -DOPENMETA_FUZZTEST_FUZZING_MODE=ON
   cmake --build build-fuzztest
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzztest/openmeta_fuzztest_metastore --fuzz_for=10s

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
