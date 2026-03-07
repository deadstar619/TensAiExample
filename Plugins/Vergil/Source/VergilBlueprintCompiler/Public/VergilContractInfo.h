#pragma once

#include "CoreMinimal.h"
#include "VergilContractInfo.generated.h"

UENUM(BlueprintType)
enum class EVergilDescriptorMatchKind : uint8
{
	Exact,
	Prefix,
	NodeKind
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilSupportedDescriptorContract
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString DescriptorContract;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilDescriptorMatchKind MatchKind = EVergilDescriptorMatchKind::Exact;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString ExpectedNodeKind = TEXT("any");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FName> SupportedTargetGraphs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FName> RequiredMetadataKeys;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Notes;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilSupportedContractManifest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 ManifestVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 PluginDescriptorVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString PluginSemanticVersion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 SchemaVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString CommandPlanFormat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 CommandPlanFormatVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FString> SupportedSchemaMigrationPaths;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FName> SupportedDocumentFields;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FName> SupportedTargetGraphs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FName> SupportedBlueprintMetadataKeys;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FString> SupportedTypeCategories;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FString> SupportedContainerTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FName> SupportedCommandTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilSupportedDescriptorContract> SupportedDescriptors;

	FString ToDisplayString() const;
};

namespace Vergil
{
	VERGILBLUEPRINTCOMPILER_API const FVergilSupportedContractManifest& GetSupportedContractManifest();
	VERGILBLUEPRINTCOMPILER_API FString DescribeSupportedContractManifest();
	VERGILBLUEPRINTCOMPILER_API FString SerializeSupportedContractManifest(bool bPrettyPrint = true);
	VERGILBLUEPRINTCOMPILER_API FString DescribeSupportedDescriptorContractsAsMarkdownTable();
}
