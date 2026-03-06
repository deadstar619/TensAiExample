#include "VergilAutomationModule.h"

#include "VergilLog.h"

void FVergilAutomationModule::StartupModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilAutomation module started."));
}

void FVergilAutomationModule::ShutdownModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilAutomation module shutting down."));
}

IMPLEMENT_MODULE(FVergilAutomationModule, VergilAutomation)
