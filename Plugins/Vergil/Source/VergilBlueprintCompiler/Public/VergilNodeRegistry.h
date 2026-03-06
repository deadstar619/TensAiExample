#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"

#include "VergilCompilerTypes.h"

class VERGILBLUEPRINTCOMPILER_API FVergilNodeRegistry final
{
public:
	static FVergilNodeRegistry& Get();

	void RegisterHandler(const TSharedRef<IVergilNodeHandler, ESPMode::ThreadSafe>& Handler);
	void RegisterFallbackHandler(const TSharedRef<IVergilNodeHandler, ESPMode::ThreadSafe>& Handler);
	TSharedPtr<IVergilNodeHandler, ESPMode::ThreadSafe> FindHandler(const FVergilGraphNode& Node) const;
	void Reset();

private:
	mutable FRWLock RegistryLock;
	TMap<FName, TSharedPtr<IVergilNodeHandler, ESPMode::ThreadSafe>> Handlers;
	TArray<TSharedPtr<IVergilNodeHandler, ESPMode::ThreadSafe>> FallbackHandlers;
};
