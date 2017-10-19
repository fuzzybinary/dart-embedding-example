========================================================================
	How I did this
========================================================================

- Set up VS environment --- vs2017 community is tricksy
-- set GYP_MSVS_VERSION=2015
-- set GYP_MSVS_OVERRIDE_PATH=c:\Program Files (x86)\Microsoft Visual Studio\2017\Community\
-- set DEPOT_TOOLS_WIN_TOOLCHAIN=0

- Don't try to use the headers / dart.lib that are included in the dart sdk. It's meant for creating dart extensions, not for embedding.
- Get the Dart SDK source according to the instructions provided

- Build the SDK. You'll need a good working copy of dart.exe
-- As of this writing, this just involves doing tools/build.py --mode=release create_sdk

- Modify some ninja files
-- Dart is configured to use "source_sets" for certain sections of code. This is great for other projects that use ninja, bad for us since we need static libraries
-- I modified runtime/vm/BUILD.gn to change the "build_libdart_vm" and "build_libdart_lib" templates to output a static library instead of a source set
- Build the libs we need using ninja directly. Go into out/ReleaseX64 and run ninja libdart_jit. This should build all the static libraries we need
-- libdart_lib_jit
-- libdart_vm_jit
-- libdart_jit
-- libdouble_conversion
-- libdart_platform

- Get Dart Snapshots from the runtime (out\ReleaseX64\gen\runtime\bin) - You need
-- isolate_snapshot_data.S
-- isolate_snapshot_instructions.S
-- vm_snapshot_data.S
-- vm_snapshot_instructions.S

- Include all .S files in the build. You need to set them up with the Microsoft Macro Assembler
-- Project -> Build Dependencies -> Check "masm"
-- Change the type of all files to "Microsoft Macro Assembler"
-- Change Preprocessor defines for all files to include _ML64_X64
-- These are what generate the externs in DartTest2.cpp

- For debugging, you need a resources associated with the service isolate. This is built with:
-- ninja gen_snapshot
- Output .cc file is then located in gen/runtime/bin/resources_gen.cc