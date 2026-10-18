# Historical comparison harness

This directory freezes the pre-cleanup compiler, evaluator, session adapter, and
interpreters for existing model audits and migration comparisons. These are test
tools, not another supported execution format.

`legacy_test_reference` and `twisty_legacy_reference` exist only in native builds
with `BUILD_TESTING=ON`. They are never installed, exported through CMake, linked
into the production CLI or WASM module, or copied into the SDK. Test executables
link either this harness or the production kernel; they must never link both.
The separate include directory prevents historical catalog layouts and APIs
from entering runtime targets. Shared compiler/core imports follow the current
API names; historical rule and replay behavior remains frozen.

Offline historical package reproducibility checks use the reference CLI. Its
saved documents and conversion API describe the historical meaning only. The
current kernel accepts relational packages/definitions and schema-2 saves.

Do not add features here. Once the geometric audits and comparison fixtures no
longer require this evaluator, replace the remaining harness with recorded
reference cases and remove it. `runtime_api_tests` and the independent installed
SDK checks exercise the actual production runtime across every preset.
