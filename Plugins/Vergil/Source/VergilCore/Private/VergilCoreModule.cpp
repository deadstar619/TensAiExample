#include "VergilCoreModule.h"

#include "VergilLog.h"

void FVergilCoreModule::StartupModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilCore module started."));
}

void FVergilCoreModule::ShutdownModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilCore module shutting down."));
}

IMPLEMENT_MODULE(FVergilCoreModule, VergilCore)
