#pragma once

#include "CoreMinimal.h"

namespace Vergil
{
	inline constexpr int32 PluginDescriptorVersion = 1;
	inline constexpr int32 SemanticVersionMajor = 0;
	inline constexpr int32 SemanticVersionMinor = 1;
	inline constexpr int32 SemanticVersionPatch = 0;
	inline constexpr int32 SchemaVersion = 3;
	inline const FName PluginName(TEXT("Vergil"));

	VERGILCORE_API FString GetSemanticVersionString();
}
