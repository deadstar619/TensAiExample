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

UENUM(BlueprintType)
enum class EVergilFunctionAccessSpecifier : uint8
{
	Public,
	Protected,
	Private
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilFunctionParameterDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilVariableTypeReference Type;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilFunctionDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bPure = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilFunctionAccessSpecifier AccessSpecifier = EVergilFunctionAccessSpecifier::Public;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilFunctionParameterDefinition> Inputs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilFunctionParameterDefinition> Outputs;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilMacroParameterDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bIsExec = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilVariableTypeReference Type;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilMacroDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilMacroParameterDefinition> Inputs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilMacroParameterDefinition> Outputs;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilComponentTransformDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bHasRelativeLocation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bHasRelativeRotation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bHasRelativeScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVector RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilComponentDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString ComponentClassPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName ParentComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName AttachSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilComponentTransformDefinition RelativeTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TMap<FName, FString> TemplateProperties;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilInterfaceDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString InterfaceClassPath;
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
	TMap<FName, FString> Metadata;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilVariableDefinition> Variables;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilFunctionDefinition> Functions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilDispatcherDefinition> Dispatchers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilMacroDefinition> Macros;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilComponentDefinition> Components;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilInterfaceDefinition> Interfaces;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TMap<FName, FString> ClassDefaults;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilGraphNode> ConstructionScriptNodes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilGraphEdge> ConstructionScriptEdges;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilGraphNode> Nodes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilGraphEdge> Edges;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FName> Tags;

	bool IsStructurallyValid(TArray<FVergilDiagnostic>* OutDiagnostics = nullptr) const;
};

UENUM(BlueprintType)
enum class EVergilDocumentDiffChangeType : uint8
{
	Added,
	Removed,
	Modified
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilDocumentDiffEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Path;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilDocumentDiffChangeType ChangeType = EVergilDocumentDiffChangeType::Modified;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString BeforeValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString AfterValue;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTMODEL_API FVergilDocumentDiff
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bDocumentsMatch = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString BeforeFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString AfterFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 AddedCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 RemovedCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 ModifiedCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilDocumentDiffEntry> Entries;

	FString ToDisplayString() const;
};

namespace Vergil
{
	VERGILBLUEPRINTMODEL_API FString GetDocumentInspectionFormatName();
	VERGILBLUEPRINTMODEL_API int32 GetDocumentInspectionFormatVersion();
	VERGILBLUEPRINTMODEL_API FString DescribeGraphDocument(const FVergilGraphDocument& Document);
	VERGILBLUEPRINTMODEL_API FString SerializeGraphDocument(const FVergilGraphDocument& Document, bool bPrettyPrint = true);
	VERGILBLUEPRINTMODEL_API FString GetDocumentDiffInspectionFormatName();
	VERGILBLUEPRINTMODEL_API int32 GetDocumentDiffInspectionFormatVersion();
	VERGILBLUEPRINTMODEL_API FVergilDocumentDiff DiffGraphDocuments(const FVergilGraphDocument& Before, const FVergilGraphDocument& After);
	VERGILBLUEPRINTMODEL_API FString DescribeDocumentDiff(const FVergilDocumentDiff& Diff);
	VERGILBLUEPRINTMODEL_API FString SerializeDocumentDiff(const FVergilDocumentDiff& Diff, bool bPrettyPrint = true);
	VERGILBLUEPRINTMODEL_API TArray<FString> GetSupportedSchemaMigrationPaths();
	VERGILBLUEPRINTMODEL_API bool CanMigrateSchemaVersion(int32 SourceSchemaVersion, int32 TargetSchemaVersion = SchemaVersion);
	VERGILBLUEPRINTMODEL_API bool MigrateDocumentSchema(
		const FVergilGraphDocument& SourceDocument,
		FVergilGraphDocument& OutDocument,
		TArray<FVergilDiagnostic>* OutDiagnostics = nullptr,
		int32 TargetSchemaVersion = SchemaVersion);
	VERGILBLUEPRINTMODEL_API bool MigrateDocumentToCurrentSchema(
		const FVergilGraphDocument& SourceDocument,
		FVergilGraphDocument& OutDocument,
		TArray<FVergilDiagnostic>* OutDiagnostics = nullptr);
}
