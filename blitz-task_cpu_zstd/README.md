# cpu_zstd

Real-world CPU benchmark: compresses a bundled data corpus with
[Zstandard](https://github.com/facebook/zstd) at the fixed default level (3) and
reports the sustained **compression throughput** (`bandwidth`, in GB/s of
uncompressed input processed). One worker thread per core runs independent
single-shot compressions of fixed 1 MiB blocks, so the score scales with both
core count and per-core speed. See [`TASK.json`](TASK.json) for the full
specification.

zstd is BSD-3-Clause (permissive), so the task itself stays under the repo's
source-available license (see the repo root `LICENSING.md`).

## Build dependencies

Beyond the repo's normal C++ toolchain (CMake ≥ 3.20, a C++17 compiler) there is
nothing to install: zstd ships a first-class CMake build, which
`common/cpp/cmake/BuildZstd.cmake` pulls in via `add_subdirectory` as a minimal
**static** library (shared lib, programs, tests and legacy formats switched off)
and exposes as the target `zstd::zstd`. This works unchanged on Linux, macOS and
Windows/MSVC.

| Dependency | Why |
|---|---|
| **zstd submodule** | `git submodule update --init external/zstd` (pinned to v1.5.6) |

## Building & running

```bash
git submodule update --init external/zstd
python build.py cpu_zstd --sample-app     # or the cmake invocation below
```

Direct CMake (from the repo root):

```bash
cmake -S blitz-task_cpu_zstd -B blitz-task_cpu_zstd/build/static \
      -DBLITZ_BUILD_MODE=STATIC -DBUILD_SAMPLE_APP=1 -DCMAKE_BUILD_TYPE=Release
cmake --build blitz-task_cpu_zstd/build/static --config Release
```

Run the sample app (`cpu_zstd_app`); it prints lifecycle events and the final
`bandwidth` with diagnostic tags (level, threads, block size, corpus and
compressed bytes, ratio, bytes processed, zstd version).

## The bundled corpus

The workload compresses a single corpus file in fixed 1 MiB blocks; the block
index is handed out round-robin to the worker threads, each of which reuses one
`ZSTD_CCtx` across its single-shot `ZSTD_compressCCtx()` calls. The build bakes
in the `assets/corpus.txt` path; at runtime it can be overridden with the
`BLITZ_ZSTD_CORPUS` environment variable. If neither resolves to a readable
file the task fails rather than measuring anything synthetic.

`assets/corpus.txt` is the public-domain text of **"Moby-Dick; or, The Whale"**
by Herman Melville (1851), from [Project Gutenberg](https://www.gutenberg.org/ebooks/2701).
The Project Gutenberg header and footer are stripped so only the public-domain
work is bundled, and line endings are normalized to LF (pinned via
`assets/.gitattributes`) so the corpus is byte-identical on every platform. Full
attribution is in
[`third_party/licenses/moby-dick/LICENSE`](../third_party/licenses/moby-dick/LICENSE),
declared in [`TASK.json`](TASK.json). To benchmark different data, point
`BLITZ_ZSTD_CORPUS` at any file.
