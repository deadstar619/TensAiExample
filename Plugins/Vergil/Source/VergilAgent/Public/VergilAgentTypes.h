#pragma once

#include "CoreMinimal.h"
#include "VergilAgentTypes.generated.h"

UENUM(BlueprintType)
enum class EVergilAgentExecutionState : uint8
{
	Pending,
	Approved,
	Completed,
	Rejected,
	Failed
};

USTRUCT(BlueprintType)
struct VERGILAGENT_API FVergilAgentRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Summary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString InputText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FName> Tags;
};

USTRUCT(BlueprintType)
struct VERGILAGENT_API FVergilAgentAuditEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilAgentRequest Request;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilAgentExecutionState State = EVergilAgentExecutionState::Pending;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Details;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString TimestampUtc;
};
