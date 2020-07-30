#pragma once

#include <memory>

#include "include/dart_api.h"

class DartIsolateGroupData;

class DartIsolate {
public:
    enum class Phase {
        //--------------------------------------------------------------------------
        /// The initial phase of all Dart isolates. This is an internal phase and
        /// callers can never get a reference to a Dart isolate in this phase.
        ///
        Unknown,
        //--------------------------------------------------------------------------
        /// The Dart isolate has been created but none of the library tag or message
        /// handers have been set yet. The is an internal phase and callers can
        /// never get a reference to a Dart isolate in this phase.
        ///
        Uninitialized,
        //--------------------------------------------------------------------------
        /// The Dart isolate has been been fully initialized but none of the
        /// libraries referenced by that isolate have been loaded yet. This is an
        /// internal phase and callers can never get a reference to a Dart isolate
        /// in this phase.
        ///
        Initialized,
        //--------------------------------------------------------------------------
        /// The isolate has been fully initialized and is waiting for the caller to
        /// associate isolate snapshots with the same. The isolate will only be
        /// ready to execute Dart code once one of the `Prepare` calls are
        /// successfully made.
        ///
        LibrariesSetup,
        //--------------------------------------------------------------------------
        /// The isolate is fully ready to start running Dart code. Callers can
        /// transition the isolate to the next state by calling the `Run` or
        /// `RunFromLibrary` methods.
        ///
        Ready,
        //--------------------------------------------------------------------------
        /// The isolate is currently running Dart code.
        ///
        Running,
        //--------------------------------------------------------------------------
        /// The isolate is no longer running Dart code and is in the middle of being
        /// collected. This is in internal phase and callers can never get a
        /// reference to a Dart isolate in this phase.
        ///
        Shutdown,
    };

    DartIsolate();

    bool Initialize(dartIsolate);

	static void DartIsolateGroupCleanupCallback(
		std::shared_ptr<DartIsolateGroupData>* isolate_group_data
	);

	static Dart_Isolate DartIsolateGroupCreateCallback(
		const char* advistory_script_uri,
		const char* advisory_script_entrypoint,
		const char* package_root,
		const char* package_config,
		Dart_IsolateFlags* flags,
		std::shared_ptr<DartIsolate>* parent_isolate_group,
		char** error
	);

    static bool DartIsolateInitializeCallback(void** child_callback_data,
        char** error);

    static Dart_Isolate DartCreateAndStartServiceIsolate(
        const char* packageRoot,
        const char* packageConfig,
        Dart_IsolateFlags* flags,
        char** error);

    static Dart_Isolate CreateDartIsolateGroup(
        std::unique_ptr<std::shared_ptr<DartIsolateGroupData>> isolate_group_data,
        std::unique_ptr<std::shared_ptr<DartIsolate>> isolate_data,
        Dart_IsolateFlags* flags,
        char** error);

    static bool InitializeIsolate(std::shared_ptr<DartIsolate> embedder_isolate,
        Dart_Isolate isolate,
        char** error);

    // |Dart_IsolateShutdownCallback|
    static void DartIsolateShutdownCallback(
        std::shared_ptr<DartIsolateGroupData>* isolate_group_data,
        std::shared_ptr<DartIsolate>* isolate_data);

    // |Dart_IsolateCleanupCallback|
    static void DartIsolateCleanupCallback(
        std::shared_ptr<DartIsolateGroupData>* isolate_group_data,
        std::shared_ptr<DartIsolate>* isolate_data);

    // |Dart_IsolateGroupCleanupCallback|
    static void DartIsolateGroupCleanupCallback(
        std::shared_ptr<DartIsolateGroupData>* isolate_group_data);

private:
    static DartIsolate* CreateRootIsolate();

    Phase _phase;
    Dart_Isolate _isolate;
};