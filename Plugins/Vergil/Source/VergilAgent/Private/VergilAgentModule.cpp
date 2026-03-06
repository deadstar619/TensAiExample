#include "VergilAgentModule.h"

#include "VergilLog.h"

void FVergilAgentModule::StartupModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilAgent module started."));
}

void FVergilAgentModule::ShutdownModule()
{
	UE_LOG(LogVergil, Verbose, TEXT("VergilAgent module shutting down."));
}

IMPLEMENT_MODULE(FVergilAgentModule, VergilAgent)
