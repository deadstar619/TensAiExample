#pragma once

#include "CoreMinimal.h"
#include "VergilReflectionInfo.generated.h"

UENUM(BlueprintType)
enum class EVergilReflectionSymbolKind : uint8
{
	None,
	Class,
	Struct,
	Enum
};

UENUM(BlueprintType)
enum class EVergilReflectionParameterDirection : uint8
{
	Input,
	Output,
	Return
};

USTRUCT(BlueprintType)
struct VERGILCORE_API FVergilReflectionParameterInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString TypeObjectPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilReflectionParameterDirection Direction = EVergilReflectionParameterDirection::Input;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bIsConst = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bIsReference = false;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILCORE_API FVergilReflectionFunctionInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString OwnerPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilReflectionParameterInfo> Parameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bBlueprintCallable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bBlueprintPure = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bConst = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bStatic = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bLatent = false;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILCORE_API FVergilReflectionPropertyInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString OwnerPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString TypeObjectPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bBlueprintVisible = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bBlueprintReadOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bEditable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bExposeOnSpawn = false;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILCORE_API FVergilReflectionEnumEntryInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int64 Value = 0;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILCORE_API FVergilReflectionSymbolInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Query;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bResolved = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilReflectionSymbolKind Kind = EVergilReflectionSymbolKind::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString ResolvedPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString SuperPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FString> CandidatePaths;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilReflectionFunctionInfo> Functions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilReflectionPropertyInfo> Properties;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilReflectionEnumEntryInfo> EnumEntries;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString FailureReason;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILCORE_API FVergilReflectionSearchResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilReflectionSymbolKind Kind = EVergilReflectionSymbolKind::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString ResolvedPath;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILCORE_API FVergilReflectionDiscoveryResults
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Query;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilReflectionSearchResult> Matches;

	FString ToDisplayString() const;
};

namespace Vergil
{
	VERGILCORE_API FString GetReflectionSymbolFormatName();
	VERGILCORE_API int32 GetReflectionSymbolFormatVersion();
	VERGILCORE_API FString DescribeReflectionSymbol(const FVergilReflectionSymbolInfo& SymbolInfo);
	VERGILCORE_API FString SerializeReflectionSymbol(const FVergilReflectionSymbolInfo& SymbolInfo, bool bPrettyPrint = true);
	VERGILCORE_API FVergilReflectionSymbolInfo InspectReflectionSymbol(const FString& Query);

	VERGILCORE_API FString GetReflectionDiscoveryFormatName();
	VERGILCORE_API int32 GetReflectionDiscoveryFormatVersion();
	VERGILCORE_API FString DescribeReflectionDiscovery(const FVergilReflectionDiscoveryResults& DiscoveryResults);
	VERGILCORE_API FString SerializeReflectionDiscovery(const FVergilReflectionDiscoveryResults& DiscoveryResults, bool bPrettyPrint = true);
	VERGILCORE_API FVergilReflectionDiscoveryResults DiscoverReflectionSymbols(const FString& Query, int32 MaxResults = 25);
}
