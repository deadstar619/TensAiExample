#include "VergilNodeRegistry.h"

FVergilNodeRegistry& FVergilNodeRegistry::Get()
{
	static FVergilNodeRegistry Registry;
	return Registry;
}

void FVergilNodeRegistry::RegisterHandler(const TSharedRef<IVergilNodeHandler, ESPMode::ThreadSafe>& Handler)
{
	FWriteScopeLock Lock(RegistryLock);
	Handlers.Add(Handler->GetDescriptor(), Handler);
}

void FVergilNodeRegistry::RegisterFallbackHandler(const TSharedRef<IVergilNodeHandler, ESPMode::ThreadSafe>& Handler)
{
	FWriteScopeLock Lock(RegistryLock);

	for (const TSharedPtr<IVergilNodeHandler, ESPMode::ThreadSafe>& ExistingHandler : FallbackHandlers)
	{
		if (ExistingHandler.IsValid() && ExistingHandler->GetDescriptor() == Handler->GetDescriptor())
		{
			return;
		}
	}

	FallbackHandlers.Add(Handler);
}

TSharedPtr<IVergilNodeHandler, ESPMode::ThreadSafe> FVergilNodeRegistry::FindHandler(const FVergilGraphNode& Node) const
{
	FReadScopeLock Lock(RegistryLock);
	if (const TSharedPtr<IVergilNodeHandler, ESPMode::ThreadSafe>* Handler = Handlers.Find(Node.Descriptor))
	{
		return *Handler;
	}

	for (const TSharedPtr<IVergilNodeHandler, ESPMode::ThreadSafe>& Handler : FallbackHandlers)
	{
		if (Handler.IsValid() && Handler->CanHandle(Node))
		{
			return Handler;
		}
	}

	return nullptr;
}

void FVergilNodeRegistry::Reset()
{
	FWriteScopeLock Lock(RegistryLock);
	Handlers.Reset();
	FallbackHandlers.Reset();
}
