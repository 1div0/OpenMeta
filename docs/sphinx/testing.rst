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

Fuzzing
-------

libFuzzer targets (Clang):

.. code-block:: bash

   cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPENMETA_BUILD_FUZZERS=ON
   cmake --build build-fuzz
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_exif_tiff_decode -max_total_time=60

FuzzTest targets (when available):

.. code-block:: bash

   cmake -S . -B build-fuzztest -G Ninja -DCMAKE_BUILD_TYPE=Debug \
     -DCMAKE_PREFIX_PATH=/mnt/f/UBSd -DOPENMETA_BUILD_FUZZTEST=ON -DOPENMETA_FUZZTEST_FUZZING_MODE=ON
   cmake --build build-fuzztest
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzztest/openmeta_fuzztest_metastore --fuzz_for=10s

