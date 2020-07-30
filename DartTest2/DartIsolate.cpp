#include "DartIsolate.h"

#include <iostream>

#include "DartHelpers.h"
#include "DartIsolateGroupData.h"


DartIsolate::DartIsolate()
{
    _phase = Phase::Uninitialized;
}

bool DartIsolate::Initialize(Dart_Isolate dartIsolate)
{
    if (_phase != Phase::Uninitialized)
        return false;

    if (dartIsolate == nullptr)
        return false;

    if (Dart_CurrentIsolate() != dartIsolate)
        return false;

    _isolate = dartIsolate;

    /// TODO: Check if this exit isolate is correct - comment from flutter:
    // We are entering a new scope (for the first time since initialization) and
    // we want to restore the current scope to null when we exit out of this
    // method. This balances the implicit Dart_EnterIsolate call made by
    // Dart_CreateIsolateGroup (which calls the Initialize).
    Dart_ExitIsolate();

    dh::DartIsolateScope scope(_isolate);

    if (LogIfError(Dart_SetLibraryTagHandler(HandleLibraryTag))
    {
        return false;
    }

}

Dart_Isolate DartIsolate::DartCreateAndStartServiceIsolate(
    const char* packageRoot,
    const char* packageConfig,
    Dart_IsolateFlags* flags,
    char** error)
{

    flags->load_vmservice_library = true;
    DartIsolate::CreateRootIsolate();
    
}

Dart_Isolate DartIsolate::DartIsolateGroupCreateCallback(
    const char* advisory_script_uri,
    const char* advisory_script_entrypoint,
    const char* package_root,
    const char* package_config,
    Dart_IsolateFlags* flags,
    std::shared_ptr<DartIsolate>* parent_isolate_data,
    char** error) 
{
    if (parent_isolate_data == nullptr &&
        strcmp(advisory_script_uri, DART_VM_SERVICE_ISOLATE_NAME) == 0) 
    {
        // The VM attempts to start the VM service for us on |Dart_Initialize|. In
        // such a case, the callback data will be null and the script URI will be
        // DART_VM_SERVICE_ISOLATE_NAME. In such cases, we just create the service
        // isolate like normal but dont hold a reference to it at all. We also start
        // this isolate since we will never again reference it from the engine.
        return DartCreateAndStartServiceIsolate(package_root,
            package_config,
            flags,
            error
        );
    }

    //DartIsolateGroupData& parent_group_data =
    //    (*parent_isolate_data)->GetIsolateGroupData();

    //auto isolate_group_data = new DartIsolateGroupData(
    //            //parent_group_data.GetSettings(),
    //            //parent_group_data.GetIsolateSnapshot(), 
    //            advisory_script_uri,
    //            advisory_script_entrypoint,
    //            //parent_group_data.GetChildIsolatePreparer(),
    //            //parent_group_data.GetIsolateCreateCallback(),
    //            p//arent_group_data.GetIsolateShutdownCallback())));

    //TaskRunners null_task_runners(advisory_script_uri,
    //    /* platform= */ nullptr, /* raster= */ nullptr,
    //    /* ui= */ nullptr,
    //    /* io= */ nullptr);

    //auto isolate_data = std::make_unique<std::shared_ptr<DartIsolate>>(
    //    std::shared_ptr<DartIsolate>(new DartIsolate(
    //    (*isolate_group_data)->GetSettings(),  // settings
    //        null_task_runners,                     // task_runners
    //        fml::WeakPtr<SnapshotDelegate>{},      // snapshot_delegate
    //        fml::WeakPtr<IOManager>{},             // io_manager
    //        fml::RefPtr<SkiaUnrefQueue>{},         // unref_queue
    //        fml::WeakPtr<ImageDecoder>{},          // image_decoder
    //        advisory_script_uri,                   // advisory_script_uri
    //        advisory_script_entrypoint,            // advisory_script_entrypoint
    //        false)));                              // is_root_isolate

    //Dart_Isolate vm_isolate = CreateDartIsolateGroup(
    //    std::move(isolate_group_data), std::move(isolate_data), flags, error);

    if (*error) {
        std::cout << "CreateDartIsolateGroup failed: " << error;
    }

    return vm_isolate;
}

// |Dart_IsolateInitializeCallback|
bool DartIsolate::DartIsolateInitializeCallback(void** child_callback_data, char** error) {
    Dart_Isolate isolate = Dart_CurrentIsolate();
    if (isolate == nullptr) {
        *error = _strdup("Isolate should be available in initialize callback.");
        std::cout << *error;
        return false;
    }

    auto* isolate_group_data =
        static_cast<std::shared_ptr<DartIsolateGroupData>*>(
            Dart_CurrentIsolateGroupData());

    TaskRunners null_task_runners((*isolate_group_data)->GetAdvisoryScriptURI(),
        /* platform= */ nullptr, /* raster= */ nullptr,
        /* ui= */ nullptr,
        /* io= */ nullptr);

    auto embedder_isolate = std::make_unique<std::shared_ptr<DartIsolate>>(
        std::shared_ptr<DartIsolate>(new DartIsolate(
        (*isolate_group_data)->GetSettings(),           // settings
            null_task_runners,                              // task_runners
            fml::WeakPtr<SnapshotDelegate>{},               // snapshot_delegate
            fml::WeakPtr<IOManager>{},                      // io_manager
            fml::RefPtr<SkiaUnrefQueue>{},                  // unref_queue
            fml::WeakPtr<ImageDecoder>{},                   // image_decoder
            (*isolate_group_data)->GetAdvisoryScriptURI(),  // advisory_script_uri
            (*isolate_group_data)
            ->GetAdvisoryScriptEntrypoint(),  // advisory_script_entrypoint
            false)));                             // is_root_isolate

    // root isolate should have been created via CreateRootIsolate
    if (!InitializeIsolate(*embedder_isolate, isolate, error)) {
        return false;
    }

    // The ownership of the embedder object is controlled by the Dart VM. So the
    // only reference returned to the caller is weak.
    *child_callback_data = embedder_isolate.release();

    Dart_EnterIsolate(isolate);
    return true;
}

Dart_Isolate DartIsolate::CreateDartIsolateGroup(
    std::unique_ptr<std::shared_ptr<DartIsolateGroupData>> isolate_group_data,
    std::unique_ptr<std::shared_ptr<DartIsolate>> isolate_data,
    Dart_IsolateFlags* flags,
    char** error) {
    
    // Create the Dart VM isolate and give it the embedder object as the baton.
    Dart_Isolate isolate = Dart_CreateIsolateGroup(
        (*isolate_group_data)->GetAdvisoryScriptURI().c_str(),
        (*isolate_group_data)->GetAdvisoryScriptEntrypoint().c_str(),
        (*isolate_group_data)->GetIsolateSnapshot()->GetDataMapping(),
        (*isolate_group_data)->GetIsolateSnapshot()->GetInstructionsMapping(),
        flags, isolate_group_data.get(), isolate_data.get(), error);

    if (isolate == nullptr) {
        return nullptr;
    }

    // Ownership of the isolate data objects has been transferred to the Dart VM.
    std::shared_ptr<DartIsolate> embedder_isolate(*isolate_data);
    isolate_group_data.release();
    isolate_data.release();

    if (!InitializeIsolate(std::move(embedder_isolate), isolate, error)) {
        return nullptr;
    }

    return isolate;
}

bool DartIsolate::InitializeIsolate(
    std::shared_ptr<DartIsolate> embedder_isolate,
    Dart_Isolate isolate,
    char** error) {
    if (!embedder_isolate->Initialize(isolate)) {
        *error = strdup("Embedder could not initialize the Dart isolate.");
        std::cout << *error;
        return false;
    }

    if (!embedder_isolate->LoadLibraries()) {
        *error = strdup(
            "Embedder could not load libraries in the new Dart isolate.");
        std::cout << *error;
        return false;
    }

    // Root isolates will be setup by the engine and the service isolate (which is
    // also a root isolate) by the utility routines in the VM. However, secondary
    // isolates will be run by the VM if they are marked as runnable.
    if (!embedder_isolate->IsRootIsolate()) {
        auto child_isolate_preparer =
            embedder_isolate->GetIsolateGroupData().GetChildIsolatePreparer();
        //FML_DCHECK(child_isolate_preparer);
        if (!child_isolate_preparer(embedder_isolate.get())) {
            *error = _strdup("Could not prepare the child isolate to run.");
            std::cout << *error;
            return false;
        }
    }

    return true;
}

// |Dart_IsolateShutdownCallback|
void DartIsolate::DartIsolateShutdownCallback(
    std::shared_ptr<DartIsolateGroupData>* isolate_group_data,
    std::shared_ptr<DartIsolate>* isolate_data) {
    isolate_data->get()->OnShutdownCallback();
}

// |Dart_IsolateGroupCleanupCallback|
void DartIsolate::DartIsolateGroupCleanupCallback(
    std::shared_ptr<DartIsolateGroupData>* isolate_data) {
    delete isolate_data;
}

// |Dart_IsolateCleanupCallback|
void DartIsolate::DartIsolateCleanupCallback(
    std::shared_ptr<DartIsolateGroupData>* isolate_group_data,
    std::shared_ptr<DartIsolate>* isolate_data) {
    delete isolate_data;
}