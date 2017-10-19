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

namespace dart {
    extern bool FLAG_trace_service;
    extern bool FLAG_trace_service_verbose;
}

extern "C" {
    extern const uint8_t kDartVmSnapshotData[];
    extern const uint8_t kDartVmSnapshotInstructions[];
    extern const uint8_t kDartCoreIsolateSnapshotData[];
    extern const uint8_t kDartCoreIsolateSnapshotInstructions[];
}

Dart_Handle core_library;

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

Dart_Handle LoadLibrary(const char* name)
{
    Dart_Handle url = Dart_NewStringFromCString(name);
    Dart_Handle library = Dart_LookupLibrary(url);

    //if (Dart_IsError(library))
    //    library = Dart_LoadLibrary(url, Dart_NewStringFromCString(source_));

    if (Dart_IsError(library)) {
        std::cerr << "Failed to load library: " << name
            << ": " << Dart_GetError(library) << std::endl;
    }

    return library;
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

Dart_Handle FilePathFromUri(Dart_Handle script, Dart_Handle core_library) {
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
    std::cout << "Resolved " << script << " to " << resolvedString;
    
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

    // Test full resolution
    /*char currentDir[MAX_PATH], realResolved[MAX_PATH];
    _getcwd(currentDir, MAX_PATH);
    sprintf_s(realResolved, MAX_PATH, "file://%s/%s", currentDir, script);
    for (int i = 0; i < strlen(realResolved); ++i) {
        if (realResolved[i] == '\\') {
            realResolved[i] = '/';
        }
    }*/

    //Dart_Handle dartRealResolved = Dart_NewStringFromCString(resolved_script);

    return Dart_LoadScript(resolved_script, resolved_script, source, 0, 0);
}

Dart_Handle LibraryTagHandler(Dart_LibraryTag tag, Dart_Handle library, Dart_Handle url)
{
    const char* url_str = NULL;
    Dart_Handle result = Dart_StringToCString(url, &url_str);
    if (Dart_IsError(result))
        return result;
    assert(false);  // TODO: implement.
    return Dart_Null();
}


bool IsServiceIsolateURL(const char* url_name) {
    return url_name != nullptr &&
        std::string(url_name) == DART_VM_SERVICE_ISOLATE_NAME;
}

Dart_Isolate ServiceIsolateCreateCallback(const char* apScriptUri, const char* apMain, const char* package_root, const char* package_config, 
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

Dart_Isolate CreateIsolate(const char* script, const char* main, const char* package_root, const char* package_config,
    Dart_IsolateFlags* flags, void* data, Dart_Handle* library, char** error)
{
    std::cout << __FUNCTION__ << ": " << script << ", " << (main ? main : "NULL") << std::endl;

    if (IsServiceIsolateURL(script))
    {
        return ServiceIsolateCreateCallback(script, main, package_root, package_config, flags, data, error);
    }
    
    dart::bin::IsolateData* isolate_data = new dart::bin::IsolateData(script, package_root, package_config, nullptr);
    Dart_Isolate isolate = Dart_CreateIsolate(script, main, kDartCoreIsolateSnapshotData, kDartCoreIsolateSnapshotInstructions, flags, isolate_data, error);
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

    // Set up uri library.
    //Dart_Handle uri_library = Isolate::uri_library->Load();
    //if (Dart_IsError(uri_library))
    //    goto fail;

    // Set up core library.
    core_library = LoadLibrary("dart:_builtin");
    if (Dart_IsError(core_library))
    {
        *error = _strdup(Dart_GetError(core_library));
        return 0;
    }

    //// Set up io library.
    //Dart_Handle io_library = Isolate::io_library->Load();
    //if (Dart_IsError(io_library))
    //    goto fail;

    std::cout << "Loaded builtin libraries" << std::endl;

    std::cout << "About to load " << script << std::endl;
    Dart_Handle lib = LoadScript(script, true, core_library);

    if (Dart_IsError(lib))
    {
        *error = _strdup(Dart_GetError(lib));
        goto fail;
    }

    result = Dart_LibraryImportLibrary(lib, core_library, Dart_Null());

    if (Dart_IsError(result))
    {
        *error = _strdup(Dart_GetError(result));
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
        return NULL;
    }

    return isolate;

fail:
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    return NULL;
}

Dart_Isolate IsolateCreateCallback(const char* script_uri, const char* main, const char
    * package_root, const char* package_config, Dart_IsolateFlags* flags, void* callback_data, char** error)
{
    Dart_Handle library;
    auto isolate = CreateIsolate(script_uri, main, package_root, package_config, flags, callback_data, &library, error);
    if (isolate == NULL)
    {
        std::cerr << "Failed to create Isolate: " << script_uri << "|" << (main ? main : "(NULL)")
            << ": " << error << std::endl;
    }

    return isolate;
}

bool IsolateInterruptCallback()
{
    return true;
}

void IsolateShutdownCallback(void* callback_data)
{

}

void IsolateUnhandledExceptionCallback(Dart_Handle error)
{

}

void *IsolateFileOpenCallback(const char* name, bool write)
{
    return NULL;
}

void IsolateFileReadCallback(const uint8_t** data, intptr_t* file_length, void* stream)
{

}

void IsolateFileWriteCallback(const void* data, intptr_t length, void* stream)
{

}

void IsolateFileCloseCallback(void* stream)
{

}

bool IsolateEntropySource(uint8_t* buffer, intptr_t length)
{
    return false;
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
    char* init_error = Dart_Initialize(&params);
    if (init_error)
    {
        std::cout << init_error;
        return 0;
    }


    Dart_IsolateFlags isolateFlags;
    Dart_IsolateFlagsInitialize(&isolateFlags);

    char* error;
    Dart_Handle library;
    Dart_Isolate isolate = CreateIsolate("hello_world.dart", "main", nullptr, nullptr, NULL, NULL, &library, &error);
    
    Dart_EnterIsolate(isolate);
    Dart_EnterScope();

    library = Dart_RootLibrary();

    Dart_SetNativeResolver(library, ResolveName, nullptr);
    

    Dart_Handle main_closure =
        Dart_GetClosure(library, Dart_NewStringFromCString("main"));
    if (!Dart_IsClosure(main_closure)) {
        std::cout << "Unable to find 'main' in root library " << "hello_world.dart";
    }

    // Call _startIsolate in the isolate library to enable dispatching the
    // initial startup message.
    const intptr_t kNumIsolateArgs = 2;
    Dart_Handle isolate_args[kNumIsolateArgs];
    isolate_args[0] = main_closure;                        // entryPoint
    isolate_args[1] = Dart_Null();

    Dart_Handle isolate_lib =
        Dart_LookupLibrary(Dart_NewStringFromCString("dart:isolate"));
    Dart_Handle result = Dart_Invoke(isolate_lib,
        Dart_NewStringFromCString("_startMainIsolate"),
        kNumIsolateArgs, isolate_args);
    
    // Keep handling messages until the last active receive port is closed.
    result = Dart_RunLoop();

    if (Dart_IsError(result))
    {
        std::cerr << "Failed to invoke main: "
            << Dart_GetError(result);
    }
    Dart_RunLoop();
    Dart_ExitScope();
    Dart_ShutdownIsolate();

    Dart_Cleanup();

    return 0;
}

