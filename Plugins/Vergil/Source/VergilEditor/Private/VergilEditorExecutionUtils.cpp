#include "VergilEditorExecutionUtils.h"

#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"

namespace
{
	FString TrimOptionalPath(const FString& Path)
	{
		return Path.TrimStartAndEnd();
	}

	FString BuildBlueprintObjectPath(const FString& BlueprintPath)
	{
		const FString PackagePath = VergilEditor::NormalizeBlueprintReference(BlueprintPath);
		if (PackagePath.IsEmpty())
		{
			return FString();
		}

		if (PackagePath.Contains(TEXT(".")))
		{
			return PackagePath;
		}

		const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		if (AssetName.IsEmpty())
		{
			return FString();
		}

		return FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);
	}

	FName InferCommandPlanTargetGraphName(const TArray<FVergilCompilerCommand>& Commands)
	{
		FName InferredGraphName = NAME_None;
		for (const FVergilCompilerCommand& Command : Commands)
		{
			if (Command.GraphName.IsNone())
			{
				continue;
			}

			if (InferredGraphName.IsNone())
			{
				InferredGraphName = Command.GraphName;
				continue;
			}

			if (InferredGraphName != Command.GraphName)
			{
				return NAME_None;
			}
		}

		return InferredGraphName;
	}

	void RefreshPreparedCommandPlan(FVergilCompileResult& Result, const bool bInferTargetGraphName)
	{
		Result.bSucceeded = true;
		for (const FVergilDiagnostic& Diagnostic : Result.Diagnostics)
		{
			if (Diagnostic.Severity == EVergilDiagnosticSeverity::Error)
			{
				Result.bSucceeded = false;
				break;
			}
		}

		if (bInferTargetGraphName && Result.Statistics.TargetGraphName.IsNone())
		{
			Result.Statistics.TargetGraphName = InferCommandPlanTargetGraphName(Result.Commands);
		}

		Result.Statistics.RebuildCommandStatistics(Result.Commands);
	}
}

FString VergilEditor::NormalizeBlueprintReference(const FString& BlueprintPath)
{
	const FString TrimmedPath = TrimOptionalPath(BlueprintPath);
	if (TrimmedPath.IsEmpty())
	{
		return FString();
	}

	if (TrimmedPath.Contains(TEXT(".")))
	{
		const FString PackagePath = FPackageName::ObjectPathToPackageName(TrimmedPath);
		return PackagePath.IsEmpty() ? TrimmedPath : PackagePath;
	}

	return TrimmedPath;
}

UBlueprint* VergilEditor::ResolveBlueprintFromReference(const FString& BlueprintPath)
{
	const FString ObjectPath = BuildBlueprintObjectPath(BlueprintPath);
	if (ObjectPath.IsEmpty())
	{
		return nullptr;
	}

	if (UBlueprint* const ExistingBlueprint = FindObject<UBlueprint>(nullptr, *ObjectPath))
	{
		return ExistingBlueprint;
	}

	return LoadObject<UBlueprint>(nullptr, *ObjectPath);
}

FVergilCompileResult VergilEditor::PrepareCommandPlanForExecution(
	const TArray<FVergilCompilerCommand>& Commands,
	const bool bInferTargetGraphName)
{
	FVergilCompileResult Result;
	Result.Commands = Commands;
	Result.Statistics.bApplyRequested = true;
	Vergil::NormalizeCommandPlan(Result.Commands);
	Result.Statistics.bCommandPlanNormalized = true;
	RefreshPreparedCommandPlan(Result, bInferTargetGraphName);
	return Result;
}
