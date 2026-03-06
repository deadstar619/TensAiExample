#include "VergilVersion.h"

FString Vergil::GetSemanticVersionString()
{
	return FString::Printf(
		TEXT("%d.%d.%d"),
		SemanticVersionMajor,
		SemanticVersionMinor,
		SemanticVersionPatch);
}
