// DartTest.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"

#include <iostream>
#include <stdarg.h>
#include <stdio.h>
#include <direct.h>

#include "include/dart_api.h"
#include "include/dart_tools_api.h"

#include "bin/vmservice_impl.h"
#include "bin/isolate_data.h"
#include "bin/utils.h"
#include "bin/eventhandler.h"
#include "bin/thread.h"

namespace dart 
{
    extern bool FLAG_trace_service;
    extern bool FLAG_trace_service_verbose;
}

extern "C"
{
    extern const uint8_t kDartVmSnapshotData[];
    extern const uint8_t kDartVmSnapshotInstructions[];
    extern const uint8_t kDartCoreIsolateSnapshotData[];
    extern const uint8_t kDartCoreIsolateSnapshotInstructions[];
}

Dart_Handle HandleError(Dart_Handle handle)
{
    if (Dart_IsError(handle))
        Dart_PropagateError(handle);
    return handle;
}

void SimplePrint(Dart_NativeArguments arguments)
{
    bool success = false;
    Dart_Handle string = HandleError(Dart_GetNativeArgument(arguments, 0));
    if (Dart_IsString(string))
    {
        const char* cstring;
        Dart_StringToCString(string, &cstring);
        std::cout << cstring;
    }
}

Dart_NativeFunction ResolveName(Dart_Handle name, int argc, bool* auto_setup_scope) {
    if (!Dart_IsString(name))
        return nullptr;
    Dart_NativeFunction result = NULL;

    const char* cname;
    HandleError(Dart_StringToCString(name, &cname));

    if (strcmp("SimplePrint", cname) == 0) result = SimplePrint;

    return result;
}

Dart_Handle Dart_Error(const char* format, ...)
{
    char message[512];

    va_list valist;
    va_start(valist, format);

    vsprintf_s(message, format, valist);
    auto handle = Dart_NewApiError(message);
    va_end(valist);

    return handle;
}

Dart_Handle FilePathFromUri(Dart_Handle script, Dart_Handle core_library) 
{
    Dart_Handle args[1] = {
        script
    };
    return Dart_Invoke(core_library, Dart_NewStringFromCString("_filePathFromUri"), 1, args);
}

Dart_Handle ReadSource(Dart_Handle script, Dart_Handle core_library)
{
    Dart_Handle script_path = FilePathFromUri(script, core_library);
    if (Dart_IsError(script_path))
        return script_path;

    const char* script_path_str;
    Dart_StringToCString(script_path, &script_path_str);

    FILE* file = nullptr;
    fopen_s(&file, script_path_str, "r");
    if (file == nullptr)
        return Dart_Error("Unable to read file '%s'", script_path_str);

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = new char[length + 1];
    size_t read = fread(buffer, 1, length, file);
    fclose(file);
    buffer[read] = '\0';

    Dart_Handle source = Dart_NewStringFromCString(buffer);

    delete[] buffer;

    return source;
}

Dart_Handle ResolveScript(const char* script, Dart_Handle core_library)
{
    const int kNumArgs = 1;
    Dart_Handle dart_args[1] = {
        Dart_NewStringFromCString(script)
    };
    Dart_Handle resolved = Dart_Invoke(core_library, Dart_NewStringFromCString("_resolveScriptUri"),
        kNumArgs, dart_args);
    
    if (Dart_IsError(resolved))
        return resolved;

    const char* resolvedString = nullptr;
    Dart_StringToCString(resolved, &resolvedString);
    
    return resolved;
}

Dart_Handle LoadScript(const char* script, bool resolve, Dart_Handle core_library)
{
    std::cout << __FUNCTION__ << ": " << script << ", " << resolve << std::endl;
    Dart_Handle resolved_script;
    Dart_Handle dart_script = Dart_NewStringFromCString(script);

    if (resolve) 
    {
        resolved_script = ResolveScript(script, core_library);
        if (Dart_IsError(resolved_script))
            return resolved_script;
    }
    else 
    {
        resolved_script = Dart_NewStringFromCString(script);
    }

    Dart_Handle source = ReadSource(resolved_script, core_library);
    if (Dart_IsError(source))
        return source;

    return Dart_LoadScript(resolved_script, resolved_script, source, 0, 0);
}

Dart_Handle LibraryTagHandler(Dart_LibraryTag tag, Dart_Handle library, Dart_Handle url)
{
    const char* url_str = NULL;
    Dart_Handle result = Dart_StringToCString(url, &url_str);
    if (Dart_IsError(result))
        return result;

    // If you want to import other files into your library, you have to implement this,
    // but for single files there's no need.
    assert(false);  
    return Dart_Null();
}


bool IsServiceIsolateURL(const char* url_name) 
{
    return url_name != nullptr &&
        std::string(url_name) == DART_VM_SERVICE_ISOLATE_NAME;
}

Dart_Isolate CreateServiceIsolate(const char* apScriptUri, const char* apMain, const char* package_root, const char* package_config, 
    Dart_IsolateFlags* apFlags, void* apCallbackData, char** aoError)
{
    dart::bin::IsolateData* isolate_data =
        new dart::bin::IsolateData(apScriptUri, package_root, package_config, nullptr);
    Dart_Isolate isolate = Dart_CreateIsolate(apScriptUri, apMain, kDartCoreIsolateSnapshotData,
        kDartCoreIsolateSnapshotInstructions, apFlags, isolate_data, aoError);

    Dart_EnterScope();

    // Re-enable this once I hear back about why the service isolate isn't actually running the vmservice package
    const char* ip = "127.0.0.1";
    const intptr_t port = 5858;
    const bool disable_websocket_origin_check = false;
    const bool service_isolate_booted = dart::bin::VmService::Setup(ip, port, false, disable_websocket_origin_check, true);
    assert(service_isolate_booted);

    Dart_ExitScope();
    Dart_ExitIsolate();
    return isolate;
}

Dart_Isolate CreateIsolate(const char* script, const char* main, const char* packageRoot, const char* packageConfig,
    Dart_IsolateFlags* flags, void* data, Dart_Handle* library, char** error)
{
    std::cout << __FUNCTION__ << ": " << script << ", " << (main ? main : "NULL") << std::endl;

    if (data == nullptr)
    {
        data = new dart::bin::IsolateData(script, packageRoot, packageConfig, nullptr);
    }
    Dart_Isolate isolate = Dart_CreateIsolate(script, main, kDartCoreIsolateSnapshotData, kDartCoreIsolateSnapshotInstructions, flags, data, error);
    assert(isolate);

    std::cout << "Created isolate" << std::endl;
    Dart_EnterScope();

    dart::bin::DartUtils::PrepareForScriptLoading(false, true);
    Dart_Handle result = Dart_SetLibraryTagHandler(LibraryTagHandler);

    if (Dart_IsError(result))
    {
        *error = _strdup(Dart_GetError(result));
        goto fail;
    }

    Dart_Handle coreLibrary = Dart_LookupLibrary(Dart_NewStringFromCString("dart:_builtin"));
    std::cout << "About to load " << script << std::endl;
    Dart_Handle lib = LoadScript(script, true, coreLibrary);

    if (Dart_IsError(lib))
    {
        *error = _strdup(Dart_GetError(lib));
        goto fail;
    }

    result = Dart_FinalizeLoading(false);
    if (Dart_IsError(result))
    {
        *error = _strdup(Dart_GetError(result));
        goto fail;
    }

    Dart_ExitScope();
    Dart_ExitIsolate();
    bool runnable = Dart_IsolateMakeRunnable(isolate);
    if (!runnable) 
    {
        *error = _strdup("Invalid isolate state - Unable to make it runnable");
        Dart_EnterIsolate(isolate);
        Dart_ShutdownIsolate();
        return nullptr;
    }

    return isolate;

fail:
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    return nullptr;
}

Dart_Isolate IsolateCreateCallback(const char* script_uri, const char* main, const char
    * package_root, const char* package_config, Dart_IsolateFlags* flags, void* callback_data, char** error)
{
    Dart_Handle library;
    dart::bin::IsolateData* isolateData = new dart::bin::IsolateData(script_uri, package_root, package_config, nullptr);

    if (IsServiceIsolateURL(script_uri))
    {
        return CreateServiceIsolate(script_uri, main, package_root, package_config, flags, isolateData, error);
    }
    else
    {
        auto isolate = CreateIsolate(script_uri, main, package_root, package_config, flags, isolateData, &library, error);
        if (isolate == nullptr)
        {
            std::cerr << "Failed to create Isolate: " << script_uri << "|" << (main ? main : "(NULL)")
                << ": " << error << std::endl;
        }
    }

    return nullptr;
}

bool IsolateInterruptCallback()
{
    return true;
}

void IsolateShutdownCallback(void* callback_data)
{
    dart::bin::IsolateData* data = (dart::bin::IsolateData*)callback_data;
    delete data;
}

void IsolateUnhandledExceptionCallback(Dart_Handle error)
{

}


int main(int argc, const char** argv)
{
    dart::FLAG_trace_service = true;
    dart::FLAG_trace_service_verbose = true;

    Dart_SetVMFlags(argc, argv);

    dart::bin::Thread::InitOnce();
    dart::bin::TimerUtils::InitOnce();
    dart::bin::EventHandler::Start();

    dart::bin::DartUtils::SetOriginalWorkingDirectory();

    Dart_InitializeParams params = {};
    params.version = DART_INITIALIZE_PARAMS_CURRENT_VERSION;
    params.vm_snapshot_data = kDartVmSnapshotData;
    params.vm_snapshot_instructions = kDartVmSnapshotInstructions;
    params.create = IsolateCreateCallback;
    params.shutdown = IsolateShutdownCallback;
    char* initError = Dart_Initialize(&params);
    if (initError)
    {
        std::cout << initError;
        return 0;
    }


    Dart_IsolateFlags isolateFlags;
    Dart_IsolateFlagsInitialize(&isolateFlags);

    char* error;
    Dart_Handle library;
    
    Dart_Isolate isolate = CreateIsolate("hello_world.dart", "main", nullptr, nullptr, nullptr, nullptr, &library, &error);
    
    Dart_EnterIsolate(isolate);
    Dart_EnterScope();

    library = Dart_RootLibrary();

    Dart_SetNativeResolver(library, ResolveName, nullptr);
    

    Dart_Handle mainClosure = Dart_GetClosure(library, Dart_NewStringFromCString("main"));
    if (!Dart_IsClosure(mainClosure))
    {
        std::cout << "Unable to find 'main' in root library " << "hello_world.dart";
    }

    // Call _startIsolate in the isolate library to enable dispatching the
    // initial startup message.
    const intptr_t kNumIsolateArgs = 2;
    Dart_Handle isolateArgs[2] = {
        mainClosure,
        Dart_Null()
    };
    Dart_Handle isolateLib = Dart_LookupLibrary(Dart_NewStringFromCString("dart:isolate"));
    Dart_Handle result = Dart_Invoke(isolateLib, Dart_NewStringFromCString("_startMainIsolate"),
        kNumIsolateArgs, isolateArgs);
    
    // Keep handling messages until the last active receive port is closed.
    result = Dart_RunLoop();

    if (Dart_IsError(result))
    {
        std::cerr << "Failed to invoke main: " << Dart_GetError(result);
    }

    Dart_ExitScope();
    Dart_ShutdownIsolate();

    Dart_Cleanup();

    return 0;
}

