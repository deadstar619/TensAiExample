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

	FString ToDisplayString() const
	{
		auto LexSeverityString = [](const EVergilDiagnosticSeverity InSeverity) -> const TCHAR*
		{
			switch (InSeverity)
			{
			case EVergilDiagnosticSeverity::Info:
				return TEXT("Info");

			case EVergilDiagnosticSeverity::Warning:
				return TEXT("Warning");

			case EVergilDiagnosticSeverity::Error:
				return TEXT("Error");

			default:
				return TEXT("Unknown");
			}
		};

		auto EscapeDisplayValue = [](const FString& Value)
		{
			FString EscapedValue = Value;
			EscapedValue.ReplaceInline(TEXT("\r"), TEXT("\\r"));
			EscapedValue.ReplaceInline(TEXT("\n"), TEXT("\\n"));
			return EscapedValue;
		};

		return FString::Printf(
			TEXT("%s code=%s source=%s message=\"%s\""),
			LexSeverityString(Severity),
			Code.IsNone() ? TEXT("<none>") : *Code.ToString(),
			SourceId.IsValid() ? *SourceId.ToString(EGuidFormats::DigitsWithHyphensLower) : TEXT("<none>"),
			*EscapeDisplayValue(Message));
	}

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
