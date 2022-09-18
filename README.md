# NOTE

I'm now working more on using Dart from a .dll / so, as well as building / linking from
a Dart repo directly. Though still in early stages, that effort is hosted 
[here](https://github.com/fuzzybinary/dart_shared_libray). I'm hoping to also include more
examples in that repo as well as instructions for building for both Mac and Linux.

# How to use this

_Updated 07/24/2021 - Updated to Dart 2.13.4_

You can clone this repo and pull out the dart directory which contains all the headers and libraries
you'll need (see Other Notes about some gotchas with these libs).

DartTest2/DartTest2.cpp does all of the embedding work. Note:

- I tried simplifying this as much as I can but there's still some complexities and code formatting
  differences between my style and the dart team. Sorry.
- You can use `Dart_Invoke` over `Dart_RunLoop` to execute a single Dart function, but doing so does
  not drain the message queue. To do so see [Draining the Message
  Queue](#draining-the-message-queue)
- Debugging should work provided the service isolate starts up correctly.
- Hot reloading works, but requires a you write your own watcher script to trigger it, as VSCode
  doesn't implement it for anything other than Flutter projects. see [this
  issue](https://github.com/Dart-Code/Dart-Code/issues/2708) for more information.
    - The [Hotreloader](https://pub.dev/packages/hotreloader) pub package implements hot reloading
      for the current process, but the code can be ported for connecting to an embedded instance.
- This does not take into account loading pre-compiled dart libraries.

Other Notes -

- This is taken from main.cc in runtime/bin which is complicated because it supports all the various
  ways of booting the dart runtime in AOT, and other modes. I would like to see how to accomplish
  that moving forward
- The startup time is high, mostly because of the overhead of booting the kernel and service
  isolates and compiling the dart.
- The way this is currently written assumes sound null safety. There is a function
  `Dart_DetectNullSafety` that you could use instead of setting the `flags->null_safety` parameter
  directly.
- This can now load a .dill precompiled kernel over a .dart file! Change `loadDill` in main to
  switch between using the .dart file and the .dill

# How I did this

Dart doesn't have anything available that makes embedding easy. The dart.lib and header files
included in the SDK are for creating extensions, not for embedding, so unfortunately, you'll have to
build it yourself.

## Get The Dart Source

Get the Dart SDK source according to the instructions provided at the Dart home page:
https://github.com/dart-lang/sdk/wiki/Building

I most recently compiled this with Visual Studio Community 2019, but 2017 is the only "supported"
version You can override the executable for building this by setting the following environment
variables

```
set GYP_MSVS_VERSION=2017
set DEPOT_TOOLS_WIN_TOOLCHAIN=0
set GYP_MSVS_OVERRIDE_PATH="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\"
```

Make sure depot_tools is also on your path **and** if you have Python 3 installed on your system
make sure depot_tools comes first in your path. Otherwise portions of the build that require Python
2 will fail.

## Build the SDK

Just in case, let's make sure everything builds properly.

As of this writing, this just involves doing `python tools/build.py --mode=release create_sdk`

## Modify some GN files

Dart needs a lot of extra stuff on top of the VM to get a lot of the embedding story working.
Instead of trying to figure out what was necessary and not, I basically created a library that is
all of dart.exe minus "main.cc" and called it "dart_lib". To do this:

- See modifications below
- Regenerate your ninja files for Dart (buildtools\gn.exe gen out\DebugX64)
- Build the library
  - Move to out\DebugX64
  - ninja libdart
- The new library will be in out\DebugX64\obj\runtime\bin
- I copied over a bunch of header files into the dart directory locally. You could just reference
  them directly if you had the dart directory in your include path. You can look in the repo and
  see what I needed to copy other than the dart_api headers

I made other changes to GN files to suit my development style. For example, I no longer use the
statically linked C runtime when I can avoid it, but dart does. If you are building this for
yourself, you may need to change these settings to suit your needs.

### Current .gn modifications

For simplicity, here are the current full modifications I made to the dart-sdk gn files for the libs
included in this repo. These modifications are current as of the version at the top of the file.

In _runtime/bin/BUILD.gn_ add the following:

```
static_library("libdart") {
  deps = [
    ":standalone_dart_io",
    "..:libdart_jit",
    "../platform:libdart_platform_jit",
    ":dart_snapshot_cc",
    ":dart_kernel_platform_cc",
    "//third_party/boringssl",
    "//third_party/zlib",
  ]
  if (dart_runtime_mode != "release") {
    deps += [ "../observatory:standalone_observatory_archive" ]
  }

  complete_static_lib = true

  if (dart_use_tcmalloc) {
    deps += [ "//third_party/tcmalloc" ]
  }

  include_dirs = [
    "..",
    "//third_party",
  ]

  sources = [
    "builtin.cc",
    "error_exit.cc",
    "error_exit.h",
    "vmservice_impl.cc",
    "vmservice_impl.h",
    "snapshot_utils.cc",
    "snapshot_utils.h",
    "gzip.cc",
    "gzip.h",
    "dfe.cc",
    "dfe.h",
    "loader.cc",
    "loader.h",
    "dart_embedder_api_impl.cc",
  ]
  if (dart_runtime_mode == "release") {
    sources += [ "observatory_assets_empty.cc" ]
  }
}
```

In _build/config/compiler/BUILD.gn_ change the following (around line 424):

```diff
- # Static CRT.
+ # Dynamic CRT.
  if (is_win) {
    if (is_debug) {
-     cflags += [ "/MTd" ]
+     cflags += [ "/MDd" ]
    } else {
-     cflags += [ "/MT" ]
+     cflags += [ "/MD" ]
    }
    defines += [
      "__STD_C",
      "_CRT_RAND_S",
      "_CRT_SECURE_NO_DEPRECATE",
+     "_ITERATOR_DEBUG_LEVEL=0",
      "_HAS_EXCEPTIONS=0",
      "_SCL_SECURE_NO_DEPRECATE",
    ]
```

# Draining the Message Queue

If you are using Dart_Invoke over Dart_RunLoop, this doesn't give dart any time to drain its message
queue or perform async operations. To get this to work, you need to invoke a private method in the
`dart:isolate` library for now. Here's the code

```cpp
Dart_Handle libraryName = Dart_NewStringFromCString("dart:isolate");
Dart_Handle isolateLib = Dart_LookupLibrary(libraryName);
if (!Dart_IsError(isolateLib))
{
    Dart_Handle invokeName = Dart_NewStringFromCString("_runPendingImmediateCallback");
    Dart_Handle result = Dart_Invoke(isolateLib, invokeName, 0, nullptr);
    if (Dart_IsError(result))
    {
        // Handle error when drainging the microtask queue
    }
    result = Dart_HandleMessage();
    if (Dart_IsError(result))
    {
        // Handle error when drainging the microtask queue
    }
}
```

# Like this?

Follow me [(@fuzzybinary)](http://twitter.com/fuzzybinary) on Twitter and let me know. I'd love to
hear from you!
