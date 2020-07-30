#pragma once

#include <string>

#include "include/dart_api.h"

// A lot of this duplicates what's in FML or Tonic, but since those
// are part of the Flutter repository I've duplicated them here
namespace dh
{
	std::string StdStringFromDart(Dart_Handle dartString);
	bool LogIfError(Dart_Handle handle);

    class DartIsolateScope
    {
    public:
        DartIsolateScope(Dart_Isolate isolate)
            : _isolate(isolate)
        {
            _previous = Dart_CurrentIsolate();
            if (_previous == _isolate)
                return;
            if (_previous)
                Dart_ExitIsolate();
            Dart_EnterIsolate(_isolate);
        }

        ~DartIsolateScope()
        {
            Dart_Isolate current = Dart_CurrentIsolate();
            if (_previous == _isolate)
                return;
            if (current)
                Dart_ExitScope();
            if (_previous)
                Dart_EnterIsolate(_previous);
        }

    private:
        Dart_Isolate _isolate;
        Dart_Isolate _previous;
    };
}