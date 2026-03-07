#pragma once

#include "CoreMinimal.h"
#include "VergilCompilerTypes.h"
#include "VergilPermissionTypes.h"
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

UENUM(BlueprintType)
enum class EVergilAgentOperation : uint8
{
	None,
	PlanDocument,
	ApplyCommandPlan
};

USTRUCT(BlueprintType)
struct VERGILAGENT_API FVergilAgentWriteAuthorization
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bApproved = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString ApprovedBy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString ApprovalNote;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILAGENT_API FVergilAgentRequestContext
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilAgentWriteAuthorization WriteAuthorization;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILAGENT_API FVergilAgentPlanPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString TargetBlueprintPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilGraphDocument Document;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName TargetGraphName = TEXT("EventGraph");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bAutoLayout = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bGenerateComments = true;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILAGENT_API FVergilAgentApplyPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString TargetBlueprintPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilCompilerCommand> Commands;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString ExpectedCommandPlanFingerprint;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILAGENT_API FVergilAgentRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilAgentRequestContext Context;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilAgentOperation Operation = EVergilAgentOperation::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilAgentPlanPayload Plan;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilAgentApplyPayload Apply;

	bool IsWriteRequest() const;
	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILAGENT_API FVergilAgentResponse
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilAgentOperation Operation = EVergilAgentOperation::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilAgentExecutionState State = EVergilAgentExecutionState::Pending;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Message;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilCompileResult Result;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILAGENT_API FVergilAgentAuditEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilAgentRequest Request;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilAgentResponse Response;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString TimestampUtc;

	FString ToDisplayString() const;
};

namespace Vergil
{
	VERGILAGENT_API FString GetAgentRequestFormatName();
	VERGILAGENT_API int32 GetAgentRequestFormatVersion();
	VERGILAGENT_API FString DescribeAgentRequest(const FVergilAgentRequest& Request);
	VERGILAGENT_API FString SerializeAgentRequest(const FVergilAgentRequest& Request, bool bPrettyPrint = true);

	VERGILAGENT_API FString GetAgentResponseFormatName();
	VERGILAGENT_API int32 GetAgentResponseFormatVersion();
	VERGILAGENT_API FString DescribeAgentResponse(const FVergilAgentResponse& Response);
	VERGILAGENT_API FString SerializeAgentResponse(const FVergilAgentResponse& Response, bool bPrettyPrint = true);

	VERGILAGENT_API FString GetAgentAuditEntryFormatName();
	VERGILAGENT_API int32 GetAgentAuditEntryFormatVersion();
	VERGILAGENT_API FString DescribeAgentAuditEntry(const FVergilAgentAuditEntry& Entry);
	VERGILAGENT_API FString SerializeAgentAuditEntry(const FVergilAgentAuditEntry& Entry, bool bPrettyPrint = true);
}
