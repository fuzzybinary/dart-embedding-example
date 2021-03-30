// DartTest.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"

#include <iostream>
#include <stdarg.h>
#include <stdio.h>
#include <direct.h>

#include "include/dart_api.h"
#include "include/dart_tools_api.h"

#include "bin/dfe.h"
#include "bin/platform.h"
#include "bin/vmservice_impl.h"
#include "bin/isolate_data.h"
#include "bin/utils.h"
#include "bin/eventhandler.h"
#include "bin/thread.h"
#include "bin/gzip.h"

using namespace dart::bin;

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

namespace dart
{
    namespace bin
    {
        extern unsigned int observatory_assets_archive_len;
        extern const uint8_t* observatory_assets_archive;
    }
}

Dart_Handle GetVMServiceAssetsArchiveCallback() 
{
    uint8_t* decompressed = NULL;
    intptr_t decompressed_len = 0;
    Decompress(observatory_assets_archive, observatory_assets_archive_len,
        &decompressed, &decompressed_len);
    Dart_Handle tar_file =
        DartUtils::MakeUint8Array(decompressed, decompressed_len);
    // Free decompressed memory as it has been copied into a Dart array.
    free(decompressed);
    return tar_file;
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

Dart_NativeFunction ResolveName(Dart_Handle name, int argc, bool* auto_setup_scope)
{
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

static Dart_Handle SetupCoreLibraries(Dart_Isolate isolate,
    IsolateData* isolate_data,
    bool is_isolate_group_start,
    const char** resolved_packages_config) {
    auto isolate_group_data = isolate_data->isolate_group_data();
    const auto packages_file = isolate_data->packages_file();
    const auto script_uri = isolate_group_data->script_url;

    Dart_Handle result;

    // Prepare builtin and other core libraries for use to resolve URIs.
    // Set up various closures, e.g: printing, timers etc.
    // Set up package configuration for URI resolution.
    result = DartUtils::PrepareForScriptLoading(false, true);
    if (Dart_IsError(result)) return result;

    // Setup packages config if specified.
    result = DartUtils::SetupPackageConfig(packages_file);
    if (Dart_IsError(result)) return result;
    if (!Dart_IsNull(result) && resolved_packages_config != nullptr) {
        result = Dart_StringToCString(result, resolved_packages_config);
        if (Dart_IsError(result)) return result;
        ASSERT(*resolved_packages_config != nullptr);
        if (is_isolate_group_start) {
            isolate_group_data->set_resolved_packages_config(
                *resolved_packages_config);
        }
        else {
            ASSERT(strcmp(isolate_group_data->resolved_packages_config(),
                *resolved_packages_config) == 0);
        }
    }

    result = Dart_SetEnvironmentCallback(DartUtils::EnvironmentCallback);
    if (Dart_IsError(result)) return result;

    // Setup the native resolver as the snapshot does not carry it.
    Builtin::SetNativeResolver(Builtin::kBuiltinLibrary);
    Builtin::SetNativeResolver(Builtin::kIOLibrary);
    Builtin::SetNativeResolver(Builtin::kCLILibrary);
    VmService::SetNativeResolver();

    result =
        DartUtils::SetupIOLibrary(NULL, script_uri, true);
    if (Dart_IsError(result)) return result;

    return Dart_Null();
}

static Dart_Isolate CreateAndSetupKernelIsolate(
    const char* script_uri,
    const char* packages_config,
    Dart_IsolateFlags* flags,
    char** error) 
{
    const char* kernel_snapshot_uri = dfe.frontend_filename();
    const char* uri =
        kernel_snapshot_uri != NULL ? kernel_snapshot_uri : script_uri;

    Dart_Isolate isolate = NULL;
    IsolateData* isolate_data = NULL;
    
    const uint8_t* kernel_service_buffer = NULL;
    intptr_t kernel_service_buffer_size = 0;
    dfe.LoadKernelService(&kernel_service_buffer, &kernel_service_buffer_size);
    ASSERT(kernel_service_buffer != NULL);
    IsolateGroupData* isolate_group_data = new IsolateGroupData(uri, packages_config, nullptr, false);
    isolate_group_data->SetKernelBufferUnowned(
        const_cast<uint8_t*>(kernel_service_buffer),
        kernel_service_buffer_size);
    isolate_data = new IsolateData(isolate_group_data);
    isolate = Dart_CreateIsolateGroupFromKernel(
        DART_KERNEL_ISOLATE_NAME, DART_KERNEL_ISOLATE_NAME, 
        kernel_service_buffer, kernel_service_buffer_size, flags, 
        isolate_group_data, isolate_data, error);

    if (isolate == NULL) {
        delete isolate_data;
        delete isolate_group_data;
        return NULL;
    }
    
    Dart_EnterScope(); 
    
    Dart_Handle result = Dart_SetLibraryTagHandler(LibraryTagHandler);
    SetupCoreLibraries(isolate, isolate_data, false, nullptr);

    Dart_Handle dartScriptUri = Dart_NewStringFromCString(script_uri);
    //CHECK_RESULT(uri);
    Dart_Handle resolved_script_uri = DartUtils::ResolveScript(dartScriptUri);
    //CHECK_RESULT(resolved_script_uri);
    result = Dart_LoadScriptFromKernel(kernel_service_buffer, kernel_service_buffer_size);
    //CHECK_RESULT(result);

    Dart_ExitScope();
    Dart_ExitIsolate();
    *error = Dart_IsolateMakeRunnable(isolate);

    return isolate;
}

Dart_Isolate CreateServiceIsolate(
    const char* script_uri, 
    const char* packages_config, 
    Dart_IsolateFlags* flags, 
    char** error)
{
    Dart_Isolate isolate = nullptr;
    auto isolate_group_data =
        new IsolateGroupData(script_uri, packages_config, nullptr, false);

    flags->load_vmservice_library = true;
    const uint8_t* isolate_snapshot_data = kDartCoreIsolateSnapshotData;
    const uint8_t* isolate_snapshot_instructions = kDartCoreIsolateSnapshotInstructions;
    isolate = Dart_CreateIsolateGroup(
        script_uri, DART_VM_SERVICE_ISOLATE_NAME, isolate_snapshot_data,
        isolate_snapshot_instructions, flags, isolate_group_data,
        /*isolate_data=*/nullptr, error);

    Dart_EnterScope();

    // Re-enable this once I hear back about why the service isolate isn't actually running the vmservice package
    const char* ip = "127.0.0.1";
    const intptr_t port = 5858;
    const bool disable_websocket_origin_check = false;
    const bool service_isolate_booted = VmService::Setup(ip, port, false, disable_websocket_origin_check,
        NULL, true, true, true, true);
    assert(service_isolate_booted);

    Dart_Handle result = Dart_SetEnvironmentCallback(DartUtils::EnvironmentCallback);
    
    Dart_ExitScope();
    Dart_ExitIsolate();
    return isolate;
}

Dart_Isolate CreateIsolate(
    bool isMainIsolate,
    const char* script_uri,
    const char* name,
    const char* packages_config,
    Dart_IsolateFlags* flags,
    void* callback_data,
    char** error)
{
    std::cout << __FUNCTION__ << ": " << script_uri << ", " << (name ? name : "NULL") << std::endl;

    uint8_t* kernel_buffer = NULL;
    intptr_t kernel_buffer_size = 0;

    PathSanitizer script_uri_sanitizer(script_uri);
    PathSanitizer packages_config_sanitizer(packages_config);
    dfe.ReadScript(script_uri, &kernel_buffer, &kernel_buffer_size);
    flags->null_safety = true;

    auto isolate_group_data = new IsolateGroupData(
        script_uri, packages_config, nullptr, false);
    isolate_group_data->SetKernelBufferNewlyOwned(kernel_buffer, kernel_buffer_size);

    const uint8_t* platform_kernel_buffer = NULL;
    intptr_t platform_kernel_buffer_size = 0;
    dfe.LoadPlatform(&platform_kernel_buffer, &platform_kernel_buffer_size);
    if (platform_kernel_buffer == NULL) {
        platform_kernel_buffer = kernel_buffer;
        platform_kernel_buffer_size = kernel_buffer_size;
    }
    
    auto isolate_data = new IsolateData(isolate_group_data);
    Dart_Isolate isolate = Dart_CreateIsolateGroupFromKernel(
        script_uri, name, platform_kernel_buffer, platform_kernel_buffer_size,
        flags, isolate_group_data, isolate_data, error);
    assert(isolate);

    std::cout << "Created isolate" << std::endl;
    Dart_EnterScope();

    Dart_Handle result = Dart_SetLibraryTagHandler(LibraryTagHandler);
    const char* resolvedPackagesConfig = nullptr;
    SetupCoreLibraries(isolate, isolate_data, true, &resolvedPackagesConfig);
    
    uint8_t* application_kernel_buffer = NULL;
    intptr_t application_kernel_buffer_size = 0;
    int exit_code = 0;
    dfe.CompileAndReadScript(script_uri, &application_kernel_buffer,
        &application_kernel_buffer_size, error, &exit_code,
        resolvedPackagesConfig,
        true);
    if (application_kernel_buffer == NULL) {
        Dart_ExitScope();
        Dart_ShutdownIsolate();
        return NULL;
    }

    isolate_group_data->SetKernelBufferNewlyOwned(
        application_kernel_buffer, application_kernel_buffer_size);
    kernel_buffer = application_kernel_buffer;
    kernel_buffer_size = application_kernel_buffer_size;
    if (kernel_buffer != NULL) {
        Dart_Handle uri = Dart_NewStringFromCString(script_uri);
        //CHECK_RESULT(uri);
        Dart_Handle resolved_script_uri = DartUtils::ResolveScript(uri);
        //CHECK_RESULT(resolved_script_uri);
        result = Dart_LoadScriptFromKernel(kernel_buffer, kernel_buffer_size);
        //CHECK_RESULT(result);
    }
    
    Dart_ExitScope();
    Dart_ExitIsolate();
    *error = Dart_IsolateMakeRunnable(isolate);
    if (*error != nullptr) 
    {
        Dart_EnterIsolate(isolate);
        Dart_ShutdownIsolate();
        return nullptr;
    }

    return isolate;
}


Dart_Isolate CreateIsolateGroupAndSetup(
    const char* script_uri,
    const char* main,
    const char* package_root,
    const char* package_config, 
    Dart_IsolateFlags* flags, 
    void* callback_data, 
    char** error)
{
    if (strcmp(script_uri, DART_VM_SERVICE_ISOLATE_NAME) == 0)
    {
        return CreateServiceIsolate(script_uri, package_config, flags, error);
    }
    else if (strcmp(script_uri, DART_KERNEL_ISOLATE_NAME) == 0) 
    {
        int exit_code;
        return CreateAndSetupKernelIsolate(script_uri, package_config, flags, error);
    }
    else
    {   
        int exitCode;
        auto isolate = CreateIsolate(false, script_uri, main, package_config, flags, callback_data, error);
        if (isolate == nullptr)
        {
            std::cerr << "Failed to create Isolate: " << script_uri << "|" << (main ? main : "(NULL)")
                << ": " << error << std::endl;
        }
    }

    return nullptr;
}

static bool OnIsolateInitialize(void** child_callback_data, char** error) 
{
    Dart_Isolate isolate = Dart_CurrentIsolate();
    ASSERT(isolate != nullptr);

    auto isolate_group_data =
        reinterpret_cast<IsolateGroupData*>(Dart_CurrentIsolateGroupData());

    auto isolate_data = new IsolateData(isolate_group_data);
    *child_callback_data = isolate_data;

    Dart_EnterScope();
    const auto script_uri = isolate_group_data->script_url;
    const bool isolate_run_app_snapshot =
        isolate_group_data->RunFromAppSnapshot();
    Dart_Handle result = SetupCoreLibraries(isolate, isolate_data,
        /*group_start=*/false,
        /*resolved_packages_config=*/nullptr);
    if (Dart_IsError(result)) goto failed;

  
    result = DartUtils::ResolveScript(Dart_NewStringFromCString(script_uri));
    if (Dart_IsError(result)) return result != nullptr;

    if (isolate_group_data->kernel_buffer().get() != nullptr) {
        // Various core-library parts will send requests to the Loader to resolve
        // relative URIs and perform other related tasks. We need Loader to be
        // initialized for this to work because loading from Kernel binary
        // bypasses normal source code loading paths that initialize it.
        const char* resolved_script_uri = NULL;
        result = Dart_StringToCString(result, &resolved_script_uri);
        if (Dart_IsError(result)) goto failed;
    }

    // Make the isolate runnable so that it is ready to handle messages.
    Dart_ExitScope();
    Dart_ExitIsolate();
    *error = Dart_IsolateMakeRunnable(isolate);
    Dart_EnterIsolate(isolate);
    return *error == nullptr;

failed:
    *error = _strdup(Dart_GetError(result));
    Dart_ExitScope();
    return false;
}

static void OnIsolateShutdown(void* isolate_group_data, void* isolate_data) 
{
    Dart_EnterScope();
    Dart_Handle sticky_error = Dart_GetStickyError();
    if (!Dart_IsNull(sticky_error) && !Dart_IsFatalError(sticky_error)) {
        printf("%s\n", Dart_GetError(sticky_error));
    }
    Dart_ExitScope();
}

static void DeleteIsolateData(void* isolate_group_data, void* callback_data) {
    auto isolate_data = reinterpret_cast<IsolateData*>(callback_data);
    delete isolate_data;
}

static void DeleteIsolateGroupData(void* callback_data) {
    auto isolate_group_data = reinterpret_cast<IsolateGroupData*>(callback_data);
    delete isolate_group_data;
}

int main(int argc, const char** argv)
{
    dart::FLAG_trace_service = true;
    //dart::FLAG_trace_service_verbose = true;

    Dart_SetVMFlags(argc, argv);
    Platform::SetExecutableName(argv[0]);
    if(!Platform::Initialize()) {
        return -1;
    }
    
    //Thread::InitOnce();
    TimerUtils::InitOnce();
    EventHandler::Start();
    
    DartUtils::SetOriginalWorkingDirectory();

    Dart_InitializeParams params = {};
    params.version = DART_INITIALIZE_PARAMS_CURRENT_VERSION;
    params.vm_snapshot_data = kDartVmSnapshotData;
    params.vm_snapshot_instructions = kDartVmSnapshotInstructions;
    params.create_group = CreateIsolateGroupAndSetup;
    params.initialize_isolate = OnIsolateInitialize;
    params.shutdown_isolate = OnIsolateShutdown;
    params.cleanup_isolate = DeleteIsolateData;
    params.cleanup_group = DeleteIsolateGroupData;
    params.entropy_source = DartUtils::EntropySource;
    params.get_service_assets = GetVMServiceAssetsArchiveCallback;
    params.start_kernel_isolate = true;
    char* initError = Dart_Initialize(&params);
    if (initError)
    {
        std::cout << initError;
        return 0;
    }


    Dart_IsolateFlags isolateFlags;
    Dart_IsolateFlagsInitialize(&isolateFlags);

    char* error;
    const char* scriptFile = "hello_world.dart";
    const char* packageConfig = ".dart_tool/package_config.json";
    Dart_Isolate isolate = CreateIsolate(true, scriptFile, "main", packageConfig, &isolateFlags, nullptr, &error);
    
    Dart_EnterIsolate(isolate);
    Dart_EnterScope();

    Dart_Handle library = Dart_RootLibrary();

    Dart_SetNativeResolver(library, ResolveName, nullptr);

    Dart_Handle mainClosure = Dart_GetField(library, Dart_NewStringFromCString("main"));
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

