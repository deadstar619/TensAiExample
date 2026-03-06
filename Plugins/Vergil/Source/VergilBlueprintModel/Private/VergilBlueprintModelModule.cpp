#include "VergilBlueprintModelModule.h"

#include "VergilLog.h"

void FVergilBlueprintModelModule::StartupModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilBlueprintModel module started."));
}

void FVergilBlueprintModelModule::ShutdownModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilBlueprintModel module shutting down."));
}

IMPLEMENT_MODULE(FVergilBlueprintModelModule, VergilBlueprintModel)
