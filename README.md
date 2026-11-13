# Spine Skeleton Data Converter

[![oosmetrics](https://api.oosmetrics.com/api/v1/badge/achievement/de213588-2f28-4fe1-9c30-07d12caeb572.svg)](https://oosmetrics.com/repo/wang606/SpineSkeletonDataConverter)

A powerful command-line tool for converting Spine skeleton data between different formats and different versions, with automatic version detection and support for multiple Spine runtime versions.

## ✨ Features

- **Automatic Version Detection**: Intelligently detects Spine version from `.skel` or `.json` files
- **Multi-Version Support**: Compatible with Spine versions 3.5, 3.6, 3.7, 3.8, 4.0, 4.1, 4.2, and 4.3
- **Smart Format Detection**: Automatically detects file formats based on file extensions
- **Cross-Platform**: Built with C++20 and CMake for maximum portability
- **Comprehensive Testing**: Includes test suites for all supported versions

## 📋 Format Support Matrix

|           | 3.5 | 3.6 | 3.7 | 3.8 | 4.0 | 4.1 | 4.2 | 4.3 |
| --------- | --- | --- | --- | --- | --- | --- | --- | --- |
| JSON Reader | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅  | ✅ |
| JSON Writer | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅  | ✅ |
| Binary Reader | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Binary Writer | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

**Legend**: ✅ Supported | 🚧 In Development | ❌ Not Supported

## 🚀 Quick Start

### Building from Source

```bash
# Clone the repository
git clone https://github.com/wang606/SpineSkeletonDataConverter.git
cd SpineSkeletonDataConverter

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
cmake --build . --config Release
```

### Usage

```bash
# Basic skeleton format conversion (same version)
SpineSkeletonDataConverter.exe input.skel output.json

# The tool auto-detects file formats from extensions
SpineSkeletonDataConverter.exe input.json output.skel

# Cross-version conversion (convert 3.7 file to 4.2 format)
SpineSkeletonDataConverter.exe input37.json output42.json -v 4.2.11

# Convert new binary format to old version
SpineSkeletonDataConverter.exe new.skel old.json -v 3.8.99

# Supported file formats:
#   .json       Spine JSON format
#   .spine-json Spine JSON for spine-godot
#   .skel       Spine binary (SKEL) format

# Options:
#   -v              Output version (must be complete: x.y.z format)
#   --remove-curve  Strip animation curves instead of converting when crossing 3.x/4.x
#   --help          Show this help message

# Supported Spine versions: 3.5.x, 3.6.x, 3.7.x, 3.8.x, 4.0.x, 4.1.x, 4.2.x, 4.3.x
# Note: Version must be specified in complete x.y.z format (e.g., 4.2.11, not 4.2)
# Input version detection is automatic based on file content.
# Output version defaults to input version unless specified with -v.
```

## 🛠️ Spine Atlas 4.x to 3.x Downgrade

A dedicated Python script for converting Spine 4.x atlas files to a 3.x compatible format.

**Features:**

- Converts `.atlas` file format, applying the `scale` property to all relevant metrics.
- Scales the associated PNG texture images according to the `scale` property.

**Usage:**
```bash
python SpineAtlasDowngrade.py input.atlas output_dir
```

The repository now also provides a native executable `SpineAtlasDowngrade` that mirrors the Python script.
Build it via CMake and run:

```
SpineAtlasDowngrade.exe input.atlas output_dir
```

When downgrading, textures are resized using stb; currently the native converter only supports PNG texture pages, so convert other formats in advance.

## 🧰 Batch Conversion Script

For large directories of Spine assets, use the bundled helper script `tools/SpineConverter.py` to drive both `SpineSkeletonDataConverter.exe` and `SpineAtlasDowngrade.exe` automatically.

```bash
python tools/SpineConverter.py <input_dir> <output_dir> \
	[-v x.y.z] \
	[--format same|json|skel|other] \
	[--remove-curve]
```

- Recursively processes `.json`, `.skel`, and `.atlas` files, preserving the folder structure in the output directory.
- `-v` overrides the target skeleton version (defaults to each file's original version).
- `--format` controls output formats for `.json`/`.skel` files (`same` keeps the original format, `json`/`skel` force a specific format, and `other` swaps between the two).
- `--remove-curve` forwards to the native converter, stripping animation curves instead of translating them when converting between 3.x and 4.x.
- `.atlas` files are always downgraded through `SpineAtlasDowngrade.exe`.

## 🧪 Testing

### Spine 4.3 Runtime compatibility

Build the converter and the independent runtime checks with CMake 3.20 or later and Python 3.8 or later.

```sh
cmake -S . -B build -DSPINE43_RUNTIME_TESTS=ON
cmake --build build --config Release --target SpineSkeletonDataConverter Spine43RuntimeCheck Spine38PoseSample Spine43PoseSample
ctest --test-dir build -C Release --output-on-failure
```

The tests compare the data loaded by the official `SkeletonJson` and `SkeletonBinary` readers. They exercise editor exports, a 3.8 binary upgrade, and fixtures for 4.3 features such as sliders, linked meshes across slots, and draw order folders. CMake fetches the pinned runtime and its examples for the tests. A local checkout of that revision can be supplied through `FETCHCONTENT_SOURCE_DIR_SPINE43_RUNTIME`. [Official Spine 4.3 C++ reader](https://github.com/EsotericSoftware/spine-runtimes/blob/405f43295b5832e86d9df3a80beb6bc9df772312/spine-cpp/src/spine/SkeletonBinary.cpp)

The 3.8 upgrade tests compare world position, rotation, scale, and the full bone transform matrix in setup pose and at 60 Hz through each animation. They cover absolute and relative local transforms, animated constraint mixes, a descendant constrained before its parent, multiple targets, skin activation, and leg IK. The 3.8 reference revision accepts the original 3.8.75 exports directly. An offline build can set `FETCHCONTENT_SOURCE_DIR_SPINE38_REFERENCE` to a checkout of that revision. [Official Spine 3.8 reference revision](https://github.com/EsotericSoftware/spine-runtimes/tree/c0699e23a0c8799710323bdf0e076e18f6ba41a2)

### Upgrading local transform constraints from 3.8

For a 3.8 to 4.3 conversion, the converter generates native constraints that wrap the live local rotation difference before mixing it. When 3.8 would re-evaluate a previously constrained bone from its animated local pose, the 4.3 output restores that pose at the corresponding point in the constraint order. Start from the original 3.8 export so the converter has the legacy constraint flags and animation keys.

The 3.8 C++ runtime divides an absolute local scale mix by the incoming scale and can accumulate absolute local shear between updates. With non-unit target scales, the converter uses the 4.3 scale mix. It reproduces the immediate applied shear and emits a warning when the authored shear values can cause accumulation. A skin-specific pose reset can be encoded for named non-default skins. Resets that depend on the absence of a skin, and default-skin-specific resets, have limited representation in SKEL. Blends across different animations and runtime edits to individual bone poses can also differ.

To compare your own assets, run the following with the executable paths for your build. Add `--channel rotate`, `translate`, `scale`, or `shear` to isolate a transform mix in both runtimes.

```sh
python tests/compare_spine_poses.py --runtime38 path/to/Spine38PoseSample --runtime43 path/to/Spine43PoseSample --report poses.json original.skel converted.spine-json converted.skel
```

For files produced by the previous converter, pass the old `.skel` as input and write a new `.skel` with `-v 4.3.2`. The converter can also read the grouped constraint fields used by its earlier 4.3 JSON output. New 4.3 JSON uses the runtime's `constraints` array.

When a legacy mesh's hull count disagrees with its triangulation, 4.3 output preserves the triangle indices and derives the hull count from them. The binary reader uses that count to determine how many triangle indices to read.

### General conversion tests

The project includes comprehensive testing tools to validate conversion accuracy and data integrity across all supported Spine versions.

For each Spine version, we recommend running three rounds of tests to ensure complete functionality:

### Round 1: JSON Round-trip Test (JSON → JSON)
Tests JSON reading and writing functionality. The generated `.json.json` files should be identical to the original `.json` files.

```bash
# Step 1: Convert all JSON files to JSON (tests JSON reader/writer)
$ python ./tools/TestSpineSkeletonDataConverter.py --exe ./build/Debug/SpineSkeletonDataConverter.exe ./data/42 .json .json

# Step 2: Compare all original vs converted JSON files
$ python ./tools/json_diff_all.py ./data/42 .json .json.json

# Step 3: Detailed comparison of specific different files (if any)
$ python ./tools/json_diff.py ./data/42/chibi-stickers/export/chibi-stickers.json ./data/42/chibi-stickers/export/chibi-stickers.json.json
```

### Round 2: SKEL to JSON Test (SKEL → JSON)
Tests SKEL reading functionality. The generated `.skel.json` files don't need to be identical to original JSON files, as SKEL and JSON formats have inherent differences.

```bash
# Step 1: Convert all SKEL files to JSON (tests SKEL reader)
$ python ./tools/TestSpineSkeletonDataConverter.py --exe ./build/Debug/SpineSkeletonDataConverter.exe ./data/42 .skel .json

# Step 2: Compare results (for reference, differences are expected)
$ python ./tools/json_diff_all.py ./data/42 .json .skel.json
```

### Round 3: Full Round-trip Test (JSON → SKEL → JSON)
Tests complete functionality: JSON reader, SKEL writer, SKEL reader, and JSON writer. The final `.json.skel.json` files should be identical to the original `.json` files.

```bash
# Step 1: Convert JSON → SKEL → JSON (tests all 4 functions)
$ python ./tools/TestSpineSkeletonDataConverter.py --exe ./build/Debug/SpineSkeletonDataConverter.exe ./data/42 .json .skel.json

# Step 2: Compare original vs final converted files
$ python ./tools/json_diff_all.py ./data/42 .json .json.skel.json

# Step 3: Detailed analysis of any differences
$ python ./tools/json_diff.py ./data/42/example/export/example.json ./data/42/example/export/example.json.skel.json
```

## 🙏 Acknowledgments

- [Spine Runtime](http://esotericsoftware.com/) - For the excellent 2D animation software
- [nlohmann/json](https://github.com/nlohmann/json) - For the JSON library
- [stb libraries](https://github.com/nothings/stb) - For lightweight image loading, resizing, and writing
- Contributors and testers who helped improve this tool

## ⚖️License
This project is licensed under the [PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/).

You may use, modify, and share this project for noncommercial purposes only, and must retain attribution to the original author.
