#pragma once

#include "CoreMinimal.h"
#include "VergilDiagnostic.h"
#include "VergilVersion.h"
#include "VergilGraphDocument.generated.h"

UENUM(BlueprintType)
enum class EVergilPinDirection : uint8
{
	Input,
	Output
};

UENUM(BlueprintType)
enum class EVergilNodeKind : uint8
{
	Event,
	Call,
	VariableGet,
	VariableSet,
	ControlFlow,
	Macro,
	Comment,
	Native,
	Custom
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilGraphPin
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilPinDirection Direction = EVergilPinDirection::Input;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName TypeName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bIsExec = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bIsArray = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString DefaultValue;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilGraphNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilNodeKind Kind = EVergilNodeKind::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Descriptor = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilGraphPin> Pins;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TMap<FName, FString> Metadata;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilGraphEdge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid SourceNodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid SourcePinId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid TargetNodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FGuid TargetPinId;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilDispatcherParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName PinCategory = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName PinSubCategory = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString ObjectPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bIsArray = false;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilDispatcherDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilDispatcherParameter> Parameters;
};

UENUM(BlueprintType)
enum class EVergilVariableContainerType : uint8
{
	None,
	Array,
	Set,
	Map
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilVariableTypeReference
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName PinCategory = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName PinSubCategory = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString ObjectPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilVariableContainerType ContainerType = EVergilVariableContainerType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName ValuePinCategory = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName ValuePinSubCategory = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString ValueObjectPath;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilVariableFlags
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bInstanceEditable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bBlueprintReadOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bExposeOnSpawn = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bPrivate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bTransient = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bSaveGame = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bAdvancedDisplay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bDeprecated = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bExposeToCinematics = false;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilVariableDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilVariableTypeReference Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilVariableFlags Flags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Category;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TMap<FName, FString> Metadata;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString DefaultValue;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilGraphDocument
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 SchemaVersion = Vergil::SchemaVersion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString BlueprintPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilVariableDefinition> Variables;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilDispatcherDefinition> Dispatchers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilGraphNode> Nodes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilGraphEdge> Edges;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FName> Tags;

	bool IsStructurallyValid(TArray<FVergilDiagnostic>* OutDiagnostics = nullptr) const;
};
