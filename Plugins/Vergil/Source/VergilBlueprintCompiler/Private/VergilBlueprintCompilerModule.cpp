#include "VergilBlueprintCompilerModule.h"

#include "VergilLog.h"

void FVergilBlueprintCompilerModule::StartupModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilBlueprintCompiler module started."));
}

void FVergilBlueprintCompilerModule::ShutdownModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilBlueprintCompiler module shutting down."));
}

IMPLEMENT_MODULE(FVergilBlueprintCompilerModule, VergilBlueprintCompiler)
