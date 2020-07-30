# How to use this

You can clone this repo and pull out the dart directory which contains all the headers and
libraries you'll need (see Other Notes about some gotchas with these libs).

DartTest2/DartTest2.cpp does all of the embedding work. Note:

- I will be working on simplifying this as much as I can and making the code style
  consistent. Right now it's all over the place.
- I will be researching what's necessary to perform this embedding `without` using
  Dart_RunLoop, since in a game (my target) I can't just have that hang forever. I will
  adjust this when I figure out how to do it properly.
- I need to check if debugging of imported libraries works. I assume it does, but I need
  to check.

Other Notes -

- This is taken from main.cc in runtime/bin which is complicated because it supports all the various
  ways of booting the dart runtime in AOT, and other modes. I would like to see how to accomplish
  that moving forward
- The startup time is high... I'm not sure why

# How I did this

_Updated 7/21/2020_

Dart doesn't have anything available that makes embedding easy. The dart.lib and header
files included in the SDK are for creating extensions, not for embedding, so unfortunately,
you'll have to build it yourself.

## Get The Dart Source

Get the Dart SDK source according to the instructions provided at the Dart home page:
https://github.com/dart-lang/sdk/wiki/Building

If you're using Visual Studio 2019 Community (like I am) you'll have to trick depot_tools into using it. Set the following environment variables:

- set GYP_MSVS_VERSION=2017
- set DEPOT_TOOLS_WIN_TOOLCHAIN=0
- GYP_MSVS_OVERRIDE_PATH=c:\Program Files (x86)\Microsoft Visual Studio\2019\Community\

Also make sure that you are using Python 2 over Python 3. The wiki has instructions on how to set the environment, but you can just make sure
Python 2 is on your path before Python 3

## Build the SDK

Just in case, let's make sure everything builds properly.

As of this writing, this just involves doing tools/build.py --mode=release create_sdk

## Modify some GN files

Dart needs a lot of extra stuff on top of the VM to get a lot of the embedding story
working. Instead of trying to figure out what was necessary and not, I basically created
a library that is all of dart.exe minus "main.cc" and called it "dart_lib". To do this:

- Edit runtime/bin/BUILD.gn and copy the template "dart_executable" and the subsequent
  target "dart"
- Rename the template "dart_runtime_library" change the "dart" target to "dart_lib"
- Add "complete_static_lib = true" to the "static_lib" directive in your new
  "dart_runtime_library template." This causes GN to bring all of the dependencies into one
  static library instead of saving them for a final executable.
- Regenerate your ninja files for Dart (buildtools\win\gn.exe gen out\DebugX64)
- Build the library
  - Move to out\DebugX64
  - ninja dart_lib
- The new library will be in out\DebugX64\obj\runtime\bin
- I copied over a bunch of header files into the dart directory locally. You could just
  reference them directly if you had the dart directory in your include path. You can
  look in theh repo and see what I needed to copy other than the dart_api headers

# Other Notes

I made other changes to GN files to suit my development style. For example, I no longer
use the statically linked C runtime when I can avoid it, but dart does. If you are building
this for yourself, you may need to change these settings to suit your needs.

These changes are made in build/config/compiler/BUILD.gn

# Like this?

Follow me [(@fuzzybinary)](http://twitter.com/fuzzybinary) on twitter and let me know. I'd love to hear from you!
