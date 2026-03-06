#pragma once

#include "CoreMinimal.h"
#include "VergilDiagnostic.generated.h"

UENUM(BlueprintType)
enum class EVergilDiagnosticSeverity : uint8
{
	Info,
	Warning,
	Error
};

USTRUCT(BlueprintType)
struct VERGILCORE_API FVergilDiagnostic
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilDiagnosticSeverity Severity = EVergilDiagnosticSeverity::Info;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Code = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Message;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid SourceId;

	static FVergilDiagnostic Make(
		const EVergilDiagnosticSeverity InSeverity,
		const FName InCode,
		const FString& InMessage,
		const FGuid& InSourceId = FGuid())
	{
		FVergilDiagnostic Diagnostic;
		Diagnostic.Severity = InSeverity;
		Diagnostic.Code = InCode;
		Diagnostic.Message = InMessage;
		Diagnostic.SourceId = InSourceId;
		return Diagnostic;
	}
};
