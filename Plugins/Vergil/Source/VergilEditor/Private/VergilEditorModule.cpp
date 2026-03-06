#include "VergilEditorModule.h"

#include "VergilLog.h"

void FVergilEditorModule::StartupModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilEditor module started."));
}

void FVergilEditorModule::ShutdownModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilEditor module shutting down."));
}

IMPLEMENT_MODULE(FVergilEditorModule, VergilEditor)
