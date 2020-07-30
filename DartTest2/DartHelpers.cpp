#include "DartHelpers.h"

#include <iostream>

namespace dh
{
    std::string StdStringFromDart(Dart_Handle dartString)
    {
        uint8_t* data = nullptr;
        intptr_t length = 0;
        if (Dart_IsError(Dart_StringToUTF8(dartString, &data, &length)))
            return std::string();
        return std::string(reinterpret_cast<char*>(data), length);
    }

    bool LogIfError(Dart_Handle handle)
    {
        if (Dart_IsUnhandledExceptionError(handle))
        {
            Dart_Handle stackTrace = Dart_ErrorGetStackTrace(handle);
            const std::string traceString = StdStringFromDart(Dart_ToString(stackTrace));
            std::cout << "Dart Unhandled Exception: " << traceString;
            return true;
        }
        else if (Dart_IsError(handle))
        {
            std::cout << "Dart Error: " << Dart_GetError(handle);
            return true;
        }
        else
        {
            return false;
        }
    }
}