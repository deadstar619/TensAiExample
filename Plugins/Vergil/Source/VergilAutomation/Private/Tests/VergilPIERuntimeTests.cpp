#include "CQTest.h"

#include "Components/MapTestSpawner.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "VergilEditorSubsystem.h"
#include "VergilGraphDocument.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS

namespace
{
	TStrongObjectPtr<UBlueprint> MakeTestBlueprint(UClass* ParentClass = nullptr)
	{
		if (ParentClass == nullptr)
		{
			ParentClass = AActor::StaticClass();
		}

		const FName BlueprintName = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_VergilPIETransient"));
		UPackage* const Package = CreatePackage(*FString::Printf(TEXT("/Temp/%s"), *BlueprintName.ToString()));
		Package->SetFlags(RF_Transient);

		return TStrongObjectPtr<UBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			Package,
			BlueprintName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			TEXT("VergilAutomation")));
	}

	FVergilVariableDefinition MakeScalarVariable(const FName Name, const FName PinCategory, const FString& DefaultValue)
	{
		FVergilVariableDefinition Variable;
		Variable.Name = Name;
		Variable.Type.PinCategory = PinCategory;
		Variable.DefaultValue = DefaultValue;
		return Variable;
	}

	FVergilVariableDefinition MakeObjectVariable(const FName Name, UClass* const Class)
	{
		FVergilVariableDefinition Variable;
		Variable.Name = Name;
		Variable.Type.PinCategory = TEXT("object");
		Variable.Type.ObjectPath = Class != nullptr ? Class->GetClassPathName().ToString() : FString();
		Variable.DefaultValue = TEXT("None");
		return Variable;
	}

	FVergilGraphNode MakeNode(const EVergilNodeKind Kind, const FName Descriptor, const FVector2D Position)
	{
		FVergilGraphNode Node;
		Node.Id = FGuid::NewGuid();
		Node.Kind = Kind;
		Node.Descriptor = Descriptor;
		Node.Position = Position;
		return Node;
	}

	FVergilGraphPin MakePin(
		const FName Name,
		const EVergilPinDirection Direction,
		const bool bIsExec = false,
		const FString& DefaultValue = FString())
	{
		FVergilGraphPin Pin;
		Pin.Id = FGuid::NewGuid();
		Pin.Name = Name;
		Pin.Direction = Direction;
		Pin.bIsExec = bIsExec;
		Pin.DefaultValue = DefaultValue;
		return Pin;
	}

	FVergilGraphEdge MakeEdge(
		const FVergilGraphNode& SourceNode,
		const FVergilGraphPin& SourcePin,
		const FVergilGraphNode& TargetNode,
		const FVergilGraphPin& TargetPin)
	{
		FVergilGraphEdge Edge;
		Edge.Id = FGuid::NewGuid();
		Edge.SourceNodeId = SourceNode.Id;
		Edge.SourcePinId = SourcePin.Id;
		Edge.TargetNodeId = TargetNode.Id;
		Edge.TargetPinId = TargetPin.Id;
		return Edge;
	}

	bool TryReadBoolProperty(const AActor* const Actor, const FName PropertyName, bool& OutValue)
	{
		if (!IsValid(Actor))
		{
			return false;
		}

		const FBoolProperty* const Property = FindFProperty<FBoolProperty>(Actor->GetClass(), PropertyName);
		if (Property == nullptr)
		{
			return false;
		}

		OutValue = Property->GetPropertyValue_InContainer(Actor);
		return true;
	}

	template <typename TObjectType>
	TObjectType* ReadObjectProperty(const AActor* const Actor, const FName PropertyName)
	{
		if (!IsValid(Actor))
		{
			return nullptr;
		}

		const FObjectPropertyBase* const Property = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), PropertyName);
		return Property != nullptr ? Cast<TObjectType>(Property->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
	}

	bool AreEventFlowFlagsObserved(const AActor* const Actor)
	{
		bool bDelayObserved = false;
		bool bTimerObserved = false;
		bool bDispatcherObserved = false;
		return TryReadBoolProperty(Actor, TEXT("DelayObserved"), bDelayObserved)
			&& TryReadBoolProperty(Actor, TEXT("TimerObserved"), bTimerObserved)
			&& TryReadBoolProperty(Actor, TEXT("DispatcherObserved"), bDispatcherObserved)
			&& bDelayObserved
			&& bTimerObserved
			&& bDispatcherObserved;
	}

	FVergilGraphDocument BuildEventFlowDocument()
	{
		FVergilGraphDocument Document;
		Document.SchemaVersion = Vergil::SchemaVersion;
		Document.BlueprintPath = TEXT("/Temp/BP_VergilPIERuntimeEventFlow");
		Document.Variables =
		{
			MakeScalarVariable(TEXT("DelaySeconds"), TEXT("float"), TEXT("0.05")),
			MakeScalarVariable(TEXT("TimerSeconds"), TEXT("float"), TEXT("0.05")),
			MakeScalarVariable(TEXT("TimerFunctionName"), TEXT("string"), TEXT("HandleTimer")),
			MakeScalarVariable(TEXT("Looping"), TEXT("bool"), TEXT("false")),
			MakeScalarVariable(TEXT("TruthValue"), TEXT("bool"), TEXT("true")),
			MakeScalarVariable(TEXT("DelayObserved"), TEXT("bool"), TEXT("false")),
			MakeScalarVariable(TEXT("TimerObserved"), TEXT("bool"), TEXT("false")),
			MakeScalarVariable(TEXT("DispatcherObserved"), TEXT("bool"), TEXT("false"))
		};

		FVergilDispatcherDefinition Dispatcher;
		Dispatcher.Name = TEXT("OnSignal");
		Document.Dispatchers.Add(Dispatcher);

		FVergilGraphNode BeginPlayNode = MakeNode(EVergilNodeKind::Event, TEXT("K2.Event.ReceiveBeginPlay"), FVector2D(0.0f, 0.0f));
		const FVergilGraphPin BeginPlayThenPin = MakePin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		BeginPlayNode.Pins.Add(BeginPlayThenPin);

		FVergilGraphNode SequenceNode = MakeNode(EVergilNodeKind::ControlFlow, TEXT("K2.Sequence"), FVector2D(260.0f, 0.0f));
		const FVergilGraphPin SequenceExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin SequenceThen0Pin = MakePin(TEXT("Then_0"), EVergilPinDirection::Output, true);
		const FVergilGraphPin SequenceThen1Pin = MakePin(TEXT("Then_1"), EVergilPinDirection::Output, true);
		SequenceNode.Pins = { SequenceExecPin, SequenceThen0Pin, SequenceThen1Pin };

		FVergilGraphNode SelfNode = MakeNode(EVergilNodeKind::Custom, TEXT("K2.Self"), FVector2D(260.0f, 220.0f));
		const FVergilGraphPin SelfOutputPin = MakePin(UEdGraphSchema_K2::PN_Self, EVergilPinDirection::Output);
		SelfNode.Pins.Add(SelfOutputPin);

		FVergilGraphNode FunctionNameGetterNode = MakeNode(EVergilNodeKind::VariableGet, TEXT("K2.VarGet.TimerFunctionName"), FVector2D(520.0f, -260.0f));
		const FVergilGraphPin FunctionNameValuePin = MakePin(TEXT("TimerFunctionName"), EVergilPinDirection::Output);
		FunctionNameGetterNode.Pins.Add(FunctionNameValuePin);

		FVergilGraphNode TimerSecondsGetterNode = MakeNode(EVergilNodeKind::VariableGet, TEXT("K2.VarGet.TimerSeconds"), FVector2D(520.0f, -140.0f));
		const FVergilGraphPin TimerSecondsValuePin = MakePin(TEXT("TimerSeconds"), EVergilPinDirection::Output);
		TimerSecondsGetterNode.Pins.Add(TimerSecondsValuePin);

		FVergilGraphNode DelaySecondsGetterNode = MakeNode(EVergilNodeKind::VariableGet, TEXT("K2.VarGet.DelaySeconds"), FVector2D(520.0f, 180.0f));
		const FVergilGraphPin DelaySecondsValuePin = MakePin(TEXT("DelaySeconds"), EVergilPinDirection::Output);
		DelaySecondsGetterNode.Pins.Add(DelaySecondsValuePin);

		FVergilGraphNode LoopingGetterNode = MakeNode(EVergilNodeKind::VariableGet, TEXT("K2.VarGet.Looping"), FVector2D(520.0f, -20.0f));
		const FVergilGraphPin LoopingValuePin = MakePin(TEXT("Looping"), EVergilPinDirection::Output);
		LoopingGetterNode.Pins.Add(LoopingValuePin);

		FVergilGraphNode TruthGetterNode = MakeNode(EVergilNodeKind::VariableGet, TEXT("K2.VarGet.TruthValue"), FVector2D(1080.0f, 380.0f));
		const FVergilGraphPin TruthValuePin = MakePin(TEXT("TruthValue"), EVergilPinDirection::Output);
		TruthGetterNode.Pins.Add(TruthValuePin);

		FVergilGraphNode HandlerEventNode = MakeNode(EVergilNodeKind::Custom, TEXT("K2.CustomEvent.HandleSignal"), FVector2D(0.0f, 520.0f));
		HandlerEventNode.Metadata.Add(TEXT("DelegatePropertyName"), TEXT("OnSignal"));
		const FVergilGraphPin HandlerEventThenPin = MakePin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		HandlerEventNode.Pins.Add(HandlerEventThenPin);

		FVergilGraphNode CreateDelegateNode = MakeNode(EVergilNodeKind::Custom, TEXT("K2.CreateDelegate.HandleSignal"), FVector2D(520.0f, 520.0f));
		const FVergilGraphPin CreateDelegateOutputPin = MakePin(TEXT("OutputDelegate"), EVergilPinDirection::Output);
		CreateDelegateNode.Pins.Add(CreateDelegateOutputPin);

		FVergilGraphNode BindNode = MakeNode(EVergilNodeKind::Custom, TEXT("K2.BindDelegate.OnSignal"), FVector2D(820.0f, 0.0f));
		const FVergilGraphPin BindExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin BindThenPin = MakePin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		const FVergilGraphPin BindSelfPin = MakePin(UEdGraphSchema_K2::PN_Self, EVergilPinDirection::Input);
		const FVergilGraphPin BindDelegatePin = MakePin(TEXT("Delegate"), EVergilPinDirection::Input);
		BindNode.Pins = { BindExecPin, BindThenPin, BindSelfPin, BindDelegatePin };

		FVergilGraphNode SetTimerNode = MakeNode(EVergilNodeKind::Call, TEXT("K2.Call.K2_SetTimer"), FVector2D(1140.0f, 0.0f));
		SetTimerNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());
		const FVergilGraphPin SetTimerExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin SetTimerObjectPin = MakePin(TEXT("Object"), EVergilPinDirection::Input);
		const FVergilGraphPin SetTimerFunctionNamePin = MakePin(TEXT("FunctionName"), EVergilPinDirection::Input);
		const FVergilGraphPin SetTimerTimePin = MakePin(TEXT("Time"), EVergilPinDirection::Input);
		const FVergilGraphPin SetTimerLoopingPin = MakePin(TEXT("bLooping"), EVergilPinDirection::Input);
		SetTimerNode.Pins = { SetTimerExecPin, SetTimerObjectPin, SetTimerFunctionNamePin, SetTimerTimePin, SetTimerLoopingPin };

		FVergilGraphNode DelayNode = MakeNode(EVergilNodeKind::Custom, TEXT("K2.Delay"), FVector2D(820.0f, 220.0f));
		const FVergilGraphPin DelayExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin DelayDurationPin = MakePin(TEXT("Duration"), EVergilPinDirection::Input);
		const FVergilGraphPin DelayThenPin = MakePin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		DelayNode.Pins = { DelayExecPin, DelayDurationPin, DelayThenPin };

		FVergilGraphNode SetDelayObservedNode = MakeNode(EVergilNodeKind::VariableSet, TEXT("K2.VarSet.DelayObserved"), FVector2D(1140.0f, 220.0f));
		const FVergilGraphPin SetDelayObservedExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin SetDelayObservedValuePin = MakePin(TEXT("DelayObserved"), EVergilPinDirection::Input);
		SetDelayObservedNode.Pins = { SetDelayObservedExecPin, SetDelayObservedValuePin };

		FVergilGraphNode TimerEventNode = MakeNode(EVergilNodeKind::Custom, TEXT("K2.CustomEvent.HandleTimer"), FVector2D(0.0f, 720.0f));
		const FVergilGraphPin TimerEventThenPin = MakePin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		TimerEventNode.Pins.Add(TimerEventThenPin);

		FVergilGraphNode SetTimerObservedNode = MakeNode(EVergilNodeKind::VariableSet, TEXT("K2.VarSet.TimerObserved"), FVector2D(420.0f, 720.0f));
		const FVergilGraphPin SetTimerObservedExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin SetTimerObservedThenPin = MakePin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		const FVergilGraphPin SetTimerObservedValuePin = MakePin(TEXT("TimerObserved"), EVergilPinDirection::Input);
		SetTimerObservedNode.Pins = { SetTimerObservedExecPin, SetTimerObservedThenPin, SetTimerObservedValuePin };

		FVergilGraphNode CallDelegateNode = MakeNode(EVergilNodeKind::Custom, TEXT("K2.CallDelegate.OnSignal"), FVector2D(760.0f, 720.0f));
		const FVergilGraphPin CallDelegateExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin CallDelegateSelfPin = MakePin(UEdGraphSchema_K2::PN_Self, EVergilPinDirection::Input);
		CallDelegateNode.Pins = { CallDelegateExecPin, CallDelegateSelfPin };

		FVergilGraphNode SetDispatcherObservedNode = MakeNode(EVergilNodeKind::VariableSet, TEXT("K2.VarSet.DispatcherObserved"), FVector2D(420.0f, 520.0f));
		const FVergilGraphPin SetDispatcherObservedExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin SetDispatcherObservedValuePin = MakePin(TEXT("DispatcherObserved"), EVergilPinDirection::Input);
		SetDispatcherObservedNode.Pins = { SetDispatcherObservedExecPin, SetDispatcherObservedValuePin };

		Document.Nodes =
		{
			BeginPlayNode,
			SequenceNode,
			SelfNode,
			FunctionNameGetterNode,
			TimerSecondsGetterNode,
			DelaySecondsGetterNode,
			LoopingGetterNode,
			TruthGetterNode,
			HandlerEventNode,
			CreateDelegateNode,
			BindNode,
			SetTimerNode,
			DelayNode,
			SetDelayObservedNode,
			TimerEventNode,
			SetTimerObservedNode,
			CallDelegateNode,
			SetDispatcherObservedNode
		};

		Document.Edges =
		{
			MakeEdge(BeginPlayNode, BeginPlayThenPin, SequenceNode, SequenceExecPin),
			MakeEdge(SequenceNode, SequenceThen0Pin, BindNode, BindExecPin),
			MakeEdge(SelfNode, SelfOutputPin, BindNode, BindSelfPin),
			MakeEdge(CreateDelegateNode, CreateDelegateOutputPin, BindNode, BindDelegatePin),
			MakeEdge(BindNode, BindThenPin, SetTimerNode, SetTimerExecPin),
			MakeEdge(SelfNode, SelfOutputPin, SetTimerNode, SetTimerObjectPin),
			MakeEdge(FunctionNameGetterNode, FunctionNameValuePin, SetTimerNode, SetTimerFunctionNamePin),
			MakeEdge(TimerSecondsGetterNode, TimerSecondsValuePin, SetTimerNode, SetTimerTimePin),
			MakeEdge(LoopingGetterNode, LoopingValuePin, SetTimerNode, SetTimerLoopingPin),
			MakeEdge(SequenceNode, SequenceThen1Pin, DelayNode, DelayExecPin),
			MakeEdge(DelaySecondsGetterNode, DelaySecondsValuePin, DelayNode, DelayDurationPin),
			MakeEdge(DelayNode, DelayThenPin, SetDelayObservedNode, SetDelayObservedExecPin),
			MakeEdge(TruthGetterNode, TruthValuePin, SetDelayObservedNode, SetDelayObservedValuePin),
			MakeEdge(TimerEventNode, TimerEventThenPin, SetTimerObservedNode, SetTimerObservedExecPin),
			MakeEdge(TruthGetterNode, TruthValuePin, SetTimerObservedNode, SetTimerObservedValuePin),
			MakeEdge(SetTimerObservedNode, SetTimerObservedThenPin, CallDelegateNode, CallDelegateExecPin),
			MakeEdge(SelfNode, SelfOutputPin, CallDelegateNode, CallDelegateSelfPin),
			MakeEdge(HandlerEventNode, HandlerEventThenPin, SetDispatcherObservedNode, SetDispatcherObservedExecPin),
			MakeEdge(TruthGetterNode, TruthValuePin, SetDispatcherObservedNode, SetDispatcherObservedValuePin)
		};

		return Document;
	}

	FVergilGraphDocument BuildWorldMutationDocument()
	{
		FVergilGraphDocument Document;
		Document.SchemaVersion = Vergil::SchemaVersion;
		Document.BlueprintPath = TEXT("/Temp/BP_VergilPIERuntimeWorldMutation");
		Document.Variables =
		{
			MakeObjectVariable(TEXT("CreatedComponent"), USceneComponent::StaticClass()),
			MakeObjectVariable(TEXT("SpawnedActor"), AActor::StaticClass())
		};

		UScriptStruct* const TransformStruct = TBaseStructure<FTransform>::Get();
		check(TransformStruct != nullptr);

		FVergilGraphNode BeginPlayNode = MakeNode(EVergilNodeKind::Event, TEXT("K2.Event.ReceiveBeginPlay"), FVector2D(0.0f, 0.0f));
		const FVergilGraphPin BeginPlayThenPin = MakePin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		BeginPlayNode.Pins.Add(BeginPlayThenPin);

		FVergilGraphNode MakeTransformNode = MakeNode(EVergilNodeKind::Custom, TEXT("K2.MakeStruct"), FVector2D(240.0f, 40.0f));
		MakeTransformNode.Metadata.Add(TEXT("StructPath"), TransformStruct->GetPathName());
		const FVergilGraphPin MakeTransformResultPin = MakePin(TransformStruct->GetFName(), EVergilPinDirection::Output);
		MakeTransformNode.Pins.Add(MakeTransformResultPin);

		FVergilGraphNode AddComponentNode = MakeNode(EVergilNodeKind::Custom, TEXT("K2.AddComponentByClass"), FVector2D(540.0f, 40.0f));
		AddComponentNode.Metadata.Add(TEXT("ComponentClassPath"), USceneComponent::StaticClass()->GetPathName());
		const FVergilGraphPin AddComponentExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin AddComponentRelativeTransformPin = MakePin(TEXT("RelativeTransform"), EVergilPinDirection::Input);
		const FVergilGraphPin AddComponentManualAttachmentPin = MakePin(TEXT("bManualAttachment"), EVergilPinDirection::Input);
		const FVergilGraphPin AddComponentThenPin = MakePin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		const FVergilGraphPin AddComponentReturnPin = MakePin(UEdGraphSchema_K2::PN_ReturnValue, EVergilPinDirection::Output);
		AddComponentNode.Pins =
		{
			AddComponentExecPin,
			AddComponentRelativeTransformPin,
			AddComponentManualAttachmentPin,
			AddComponentThenPin,
			AddComponentReturnPin
		};

		FVergilGraphNode SetCreatedComponentNode = MakeNode(EVergilNodeKind::VariableSet, TEXT("K2.VarSet.CreatedComponent"), FVector2D(920.0f, 40.0f));
		const FVergilGraphPin SetCreatedComponentExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin SetCreatedComponentThenPin = MakePin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		const FVergilGraphPin SetCreatedComponentValuePin = MakePin(TEXT("CreatedComponent"), EVergilPinDirection::Input);
		SetCreatedComponentNode.Pins = { SetCreatedComponentExecPin, SetCreatedComponentThenPin, SetCreatedComponentValuePin };

		FVergilGraphNode SpawnActorNode = MakeNode(EVergilNodeKind::Custom, TEXT("K2.SpawnActor"), FVector2D(1260.0f, 40.0f));
		SpawnActorNode.Metadata.Add(TEXT("ActorClassPath"), AActor::StaticClass()->GetPathName());
		const FVergilGraphPin SpawnActorExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin SpawnActorTransformPin = MakePin(TEXT("SpawnTransform"), EVergilPinDirection::Input);
		const FVergilGraphPin SpawnActorThenPin = MakePin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		const FVergilGraphPin SpawnActorReturnPin = MakePin(UEdGraphSchema_K2::PN_ReturnValue, EVergilPinDirection::Output);
		SpawnActorNode.Pins =
		{
			SpawnActorExecPin,
			SpawnActorTransformPin,
			SpawnActorThenPin,
			SpawnActorReturnPin
		};

		FVergilGraphNode SetSpawnedActorNode = MakeNode(EVergilNodeKind::VariableSet, TEXT("K2.VarSet.SpawnedActor"), FVector2D(1620.0f, 40.0f));
		const FVergilGraphPin SetSpawnedActorExecPin = MakePin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
		const FVergilGraphPin SetSpawnedActorValuePin = MakePin(TEXT("SpawnedActor"), EVergilPinDirection::Input);
		SetSpawnedActorNode.Pins = { SetSpawnedActorExecPin, SetSpawnedActorValuePin };

		Document.Nodes =
		{
			BeginPlayNode,
			MakeTransformNode,
			AddComponentNode,
			SetCreatedComponentNode,
			SpawnActorNode,
			SetSpawnedActorNode
		};

		Document.Edges =
		{
			MakeEdge(BeginPlayNode, BeginPlayThenPin, AddComponentNode, AddComponentExecPin),
			MakeEdge(MakeTransformNode, MakeTransformResultPin, AddComponentNode, AddComponentRelativeTransformPin),
			MakeEdge(AddComponentNode, AddComponentReturnPin, SetCreatedComponentNode, SetCreatedComponentValuePin),
			MakeEdge(AddComponentNode, AddComponentThenPin, SetCreatedComponentNode, SetCreatedComponentExecPin),
			MakeEdge(SetCreatedComponentNode, SetCreatedComponentThenPin, SpawnActorNode, SpawnActorExecPin),
			MakeEdge(MakeTransformNode, MakeTransformResultPin, SpawnActorNode, SpawnActorTransformPin),
			MakeEdge(SpawnActorNode, SpawnActorReturnPin, SetSpawnedActorNode, SetSpawnedActorValuePin),
			MakeEdge(SpawnActorNode, SpawnActorThenPin, SetSpawnedActorNode, SetSpawnedActorExecPin)
		};

		return Document;
	}
}

TEST_CLASS_WITH_FLAGS(FVergilPIERuntimeValidationTests, "Vergil.Scaffold.PIERuntime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
{
	TUniquePtr<FMapTestSpawner> Spawner{ nullptr };
	TStrongObjectPtr<UBlueprint> CurrentBlueprint;
	UVergilEditorSubsystem* EditorSubsystem = nullptr;
	AActor* RuntimeActor = nullptr;

	BEFORE_EACH()
	{
		EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
		ASSERT_THAT(IsNotNull(EditorSubsystem, TEXT("Vergil editor subsystem should be available.")));

		Spawner = FMapTestSpawner::CreateFromTempLevel(TestCommandBuilder);
		ASSERT_THAT(IsNotNull(Spawner, TEXT("PIE map spawner should be available.")));

		Spawner->AddWaitUntilLoadedCommand(TestRunner);
	}

	AFTER_EACH()
	{
		RuntimeActor = nullptr;
		CurrentBlueprint.Reset();
		Spawner.Reset();
	}

	TEST_METHOD(EventFlowRuntimeValidation)
	{
		CurrentBlueprint = MakeTestBlueprint(AActor::StaticClass());
		ASSERT_THAT(IsNotNull(CurrentBlueprint.Get(), TEXT("Transient PIE runtime blueprint should be created.")));

		const FVergilCompileResult Result = EditorSubsystem->CompileDocument(CurrentBlueprint.Get(), BuildEventFlowDocument(), false, false, true);
		ASSERT_THAT(IsTrue(Result.bSucceeded, TEXT("PIE event-flow document should compile successfully.")));
		ASSERT_THAT(IsTrue(Result.bApplied, TEXT("PIE event-flow document should apply successfully.")));
		ASSERT_THAT(IsNotNull(CurrentBlueprint->GeneratedClass.Get(), TEXT("PIE event-flow blueprint should expose a generated class.")));

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		TestCommandBuilder
			.Do(TEXT("Spawn runtime event-flow actor"), [this, SpawnParameters]()
			{
				RuntimeActor = &Spawner->SpawnActor<AActor>(SpawnParameters, CurrentBlueprint->GeneratedClass.Get());
			})
			.Until(TEXT("Wait for latent event flow"), [this]()
			{
				return AreEventFlowFlagsObserved(RuntimeActor);
			}, FTimespan::FromSeconds(5.0))
			.Then(TEXT("Assert latent event flow"), [this]()
			{
				bool bDelayObserved = false;
				bool bTimerObserved = false;
				bool bDispatcherObserved = false;
				ASSERT_THAT(IsNotNull(RuntimeActor, TEXT("Runtime actor should still be valid.")));
				ASSERT_THAT(IsTrue(TryReadBoolProperty(RuntimeActor, TEXT("DelayObserved"), bDelayObserved) && bDelayObserved, TEXT("DelayObserved should become true during PIE.")));
				ASSERT_THAT(IsTrue(TryReadBoolProperty(RuntimeActor, TEXT("TimerObserved"), bTimerObserved) && bTimerObserved, TEXT("TimerObserved should become true during PIE.")));
				ASSERT_THAT(IsTrue(TryReadBoolProperty(RuntimeActor, TEXT("DispatcherObserved"), bDispatcherObserved) && bDispatcherObserved, TEXT("DispatcherObserved should become true during PIE.")));
			});
	}

	TEST_METHOD(WorldMutationRuntimeValidation)
	{
		CurrentBlueprint = MakeTestBlueprint(AActor::StaticClass());
		ASSERT_THAT(IsNotNull(CurrentBlueprint.Get(), TEXT("Transient PIE runtime blueprint should be created.")));

		const FVergilCompileResult Result = EditorSubsystem->CompileDocument(CurrentBlueprint.Get(), BuildWorldMutationDocument(), false, false, true);
		ASSERT_THAT(IsTrue(Result.bSucceeded, TEXT("PIE world-mutation document should compile successfully.")));
		ASSERT_THAT(IsTrue(Result.bApplied, TEXT("PIE world-mutation document should apply successfully.")));
		ASSERT_THAT(IsNotNull(CurrentBlueprint->GeneratedClass.Get(), TEXT("PIE world-mutation blueprint should expose a generated class.")));

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		TestCommandBuilder
			.Do(TEXT("Spawn runtime world-mutation actor"), [this, SpawnParameters]()
			{
				RuntimeActor = &Spawner->SpawnActor<AActor>(SpawnParameters, CurrentBlueprint->GeneratedClass.Get());
			})
			.Until(TEXT("Wait for runtime world mutation"), [this]()
			{
				return ReadObjectProperty<USceneComponent>(RuntimeActor, TEXT("CreatedComponent")) != nullptr
					&& ReadObjectProperty<AActor>(RuntimeActor, TEXT("SpawnedActor")) != nullptr;
			}, FTimespan::FromSeconds(5.0))
			.Then(TEXT("Assert runtime world mutation"), [this]()
			{
				USceneComponent* const CreatedComponent = ReadObjectProperty<USceneComponent>(RuntimeActor, TEXT("CreatedComponent"));
				AActor* const SpawnedActor = ReadObjectProperty<AActor>(RuntimeActor, TEXT("SpawnedActor"));
				ASSERT_THAT(IsNotNull(RuntimeActor, TEXT("Runtime actor should still be valid.")));
				ASSERT_THAT(IsNotNull(CreatedComponent, TEXT("CreatedComponent should be assigned during PIE.")));
				ASSERT_THAT(IsNotNull(SpawnedActor, TEXT("SpawnedActor should be assigned during PIE.")));
				ASSERT_THAT(IsTrue(CreatedComponent != nullptr && CreatedComponent->GetOwner() == RuntimeActor, TEXT("Created component should belong to the runtime actor.")));
				ASSERT_THAT(IsTrue(CreatedComponent != nullptr && CreatedComponent->GetWorld() == RuntimeActor->GetWorld(), TEXT("Created component should live in the runtime actor world.")));
				ASSERT_THAT(IsTrue(SpawnedActor != nullptr && SpawnedActor->GetWorld() == RuntimeActor->GetWorld(), TEXT("Spawned actor should live in the same PIE world as the runtime actor.")));
				ASSERT_THAT(IsTrue(SpawnedActor != RuntimeActor, TEXT("Spawned actor should be distinct from the runtime actor.")));
			});
	}
};

#endif // WITH_EDITOR && WITH_AUTOMATION_TESTS
