#pragma once

#include "CoreMinimal.h"
#include "VergilCommandTypes.generated.h"

UENUM(BlueprintType)
enum class EVergilCommandType : uint8
{
	EnsureDispatcher,
	AddDispatcherParameter,
	SetBlueprintMetadata,
	EnsureVariable,
	SetVariableMetadata,
	SetVariableDefault,
	EnsureFunctionGraph,
	EnsureMacroGraph,
	EnsureComponent,
	AttachComponent,
	SetComponentProperty,
	EnsureInterface,
	SetClassDefault,
	EnsureGraph,
	AddNode,
	SetNodeMetadata,
	ConnectPins,
	RemoveNode,
	RenameMember,
	MoveNode,
	FinalizeNode,
	CompileBlueprint
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilPlannedPin
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid PinId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bIsInput = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bIsExec = false;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilCompilerCommand
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilCommandType Type = EVergilCommandType::AddNode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName GraphName = TEXT("EventGraph");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid NodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid SourceNodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid SourcePinId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid TargetNodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid TargetPinId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName SecondaryName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString StringValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TMap<FName, FString> Attributes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilPlannedPin> PlannedPins;
};
