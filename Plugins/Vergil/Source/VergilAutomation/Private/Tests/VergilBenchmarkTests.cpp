#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "VergilEditorSubsystem.h"
#include "VergilGraphDocument.h"

namespace
{
	struct FVergilLargeGraphBenchmarkScenario final
	{
		FString Name;
		int32 SegmentCount = 0;
		int32 CommentStride = 0;
		double MaxPlanSeconds = 0.0;
		double MaxApplySeconds = 0.0;
	};

	struct FVergilLargeGraphBenchmarkPhaseResult final
	{
		bool bSucceeded = false;
		double ElapsedSeconds = 0.0;
		int32 DiagnosticCount = 0;
		int32 CommandCount = 0;
		int32 ExecutedCommandCount = 0;
		FString CommandPlanFingerprint;
	};

	struct FVergilLargeGraphBenchmarkResult final
	{
		FString Name;
		int32 SegmentCount = 0;
		int32 CommentCount = 0;
		int32 DocumentNodeCount = 0;
		int32 DocumentEdgeCount = 0;
		int32 AppliedGraphNodeCount = 0;
		double MaxPlanSeconds = 0.0;
		double MaxApplySeconds = 0.0;
		FVergilLargeGraphBenchmarkPhaseResult Plan;
		FVergilLargeGraphBenchmarkPhaseResult Apply;
	};

	FVergilLargeGraphBenchmarkScenario MakeBenchmarkScenario(
		const TCHAR* Name,
		const int32 SegmentCount,
		const int32 CommentStride,
		const double MaxPlanSeconds,
		const double MaxApplySeconds)
	{
		FVergilLargeGraphBenchmarkScenario Scenario;
		Scenario.Name = Name;
		Scenario.SegmentCount = SegmentCount;
		Scenario.CommentStride = CommentStride;
		Scenario.MaxPlanSeconds = MaxPlanSeconds;
		Scenario.MaxApplySeconds = MaxApplySeconds;
		return Scenario;
	}

	TStrongObjectPtr<UBlueprint> MakeBenchmarkTransientBlueprint(UClass* ParentClass = nullptr)
	{
		if (ParentClass == nullptr)
		{
			ParentClass = AActor::StaticClass();
		}

		const FName BlueprintName = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_VergilBenchmarkTransient"));
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

	FVergilVariableDefinition MakeBenchmarkBoolVariable(const FName Name, const FString& DefaultValue)
	{
		FVergilVariableDefinition Variable;
		Variable.Name = Name;
		Variable.Type.PinCategory = TEXT("bool");
		Variable.DefaultValue = DefaultValue;
		return Variable;
	}

	FVergilGraphNode MakeBenchmarkNode(const EVergilNodeKind Kind, const FName Descriptor, const FVector2D Position)
	{
		FVergilGraphNode Node;
		Node.Id = FGuid::NewGuid();
		Node.Kind = Kind;
		Node.Descriptor = Descriptor;
		Node.Position = Position;
		return Node;
	}

	FVergilGraphPin MakeBenchmarkPin(
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

	FVergilGraphEdge MakeBenchmarkEdge(
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

	FString GetBenchmarkSummaryPath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Vergil"), TEXT("Benchmarks"), TEXT("LargeGraphBenchmarkSummary.json"));
	}

	int32 CountBenchmarkEventGraphNodes(UBlueprint* const Blueprint)
	{
		UEdGraph* const EventGraph = Blueprint != nullptr ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
		return EventGraph != nullptr ? EventGraph->Nodes.Num() : 0;
	}

	FVergilGraphDocument BuildBenchmarkLargeGraphDocument(const FVergilLargeGraphBenchmarkScenario& Scenario, int32& OutCommentCount)
	{
		OutCommentCount = 0;

		FVergilGraphDocument Document;
		Document.SchemaVersion = Vergil::SchemaVersion;
		Document.BlueprintPath = FString::Printf(TEXT("/Temp/BP_VergilLargeGraphBenchmark_%s"), *Scenario.Name);
		Document.Variables = { MakeBenchmarkBoolVariable(TEXT("BenchmarkFlag"), TEXT("false")) };

		FVergilGraphNode BeginPlayNode = MakeBenchmarkNode(EVergilNodeKind::Event, TEXT("K2.Event.ReceiveBeginPlay"), FVector2D::ZeroVector);
		const FVergilGraphPin BeginPlayThenPin = MakeBenchmarkPin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
		BeginPlayNode.Pins.Add(BeginPlayThenPin);
		Document.Nodes.Add(BeginPlayNode);

		FVergilGraphNode PreviousExecNode = BeginPlayNode;
		FVergilGraphPin PreviousExecPin = BeginPlayThenPin;

		for (int32 SegmentIndex = 0; SegmentIndex < Scenario.SegmentCount; ++SegmentIndex)
		{
			const int32 RowIndex = SegmentIndex / 16;
			const int32 ColumnIndex = SegmentIndex % 16;
			const float BaseX = static_cast<float>(ColumnIndex) * 900.0f;
			const float BaseY = static_cast<float>(RowIndex) * 280.0f;

			FVergilGraphNode GetterNode = MakeBenchmarkNode(
				EVergilNodeKind::VariableGet,
				TEXT("K2.VarGet.BenchmarkFlag"),
				FVector2D(BaseX, BaseY + 120.0f));
			const FVergilGraphPin GetterValuePin = MakeBenchmarkPin(TEXT("BenchmarkFlag"), EVergilPinDirection::Output);
			GetterNode.Pins.Add(GetterValuePin);

			FVergilGraphNode NotNode = MakeBenchmarkNode(
				EVergilNodeKind::Call,
				TEXT("K2.Call.Not_PreBool"),
				FVector2D(BaseX + 280.0f, BaseY + 120.0f));
			NotNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetMathLibrary::StaticClass()->GetClassPathName().ToString());
			const FVergilGraphPin NotInputPin = MakeBenchmarkPin(TEXT("A"), EVergilPinDirection::Input);
			const FVergilGraphPin NotReturnPin = MakeBenchmarkPin(UEdGraphSchema_K2::PN_ReturnValue, EVergilPinDirection::Output);
			NotNode.Pins = { NotInputPin, NotReturnPin };

			FVergilGraphNode SetterNode = MakeBenchmarkNode(
				EVergilNodeKind::VariableSet,
				TEXT("K2.VarSet.BenchmarkFlag"),
				FVector2D(BaseX + 560.0f, BaseY + 80.0f));
			const FVergilGraphPin SetterExecPin = MakeBenchmarkPin(UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true);
			const FVergilGraphPin SetterThenPin = MakeBenchmarkPin(UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true);
			const FVergilGraphPin SetterValuePin = MakeBenchmarkPin(TEXT("BenchmarkFlag"), EVergilPinDirection::Input);
			SetterNode.Pins = { SetterExecPin, SetterThenPin, SetterValuePin };

			Document.Nodes.Add(GetterNode);
			Document.Nodes.Add(NotNode);
			Document.Nodes.Add(SetterNode);

			Document.Edges.Add(MakeBenchmarkEdge(PreviousExecNode, PreviousExecPin, SetterNode, SetterExecPin));
			Document.Edges.Add(MakeBenchmarkEdge(GetterNode, GetterValuePin, NotNode, NotInputPin));
			Document.Edges.Add(MakeBenchmarkEdge(NotNode, NotReturnPin, SetterNode, SetterValuePin));

			PreviousExecNode = SetterNode;
			PreviousExecPin = SetterThenPin;

			if (Scenario.CommentStride > 0 && SegmentIndex % Scenario.CommentStride == 0)
			{
				FVergilGraphNode CommentNode = MakeBenchmarkNode(
					EVergilNodeKind::Comment,
					TEXT("UI.Comment"),
					FVector2D(BaseX - 120.0f, BaseY - 60.0f));
				CommentNode.Metadata.Add(TEXT("CommentText"), FString::Printf(TEXT("Benchmark block %d"), SegmentIndex / Scenario.CommentStride));
				CommentNode.Metadata.Add(TEXT("CommentWidth"), TEXT("780"));
				Document.Nodes.Add(CommentNode);
				++OutCommentCount;
			}
		}

		return Document;
	}

	void WriteBenchmarkSummaryFile(const TArray<FVergilLargeGraphBenchmarkResult>& Results, FString& OutSummaryPath)
	{
		OutSummaryPath = GetBenchmarkSummaryPath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutSummaryPath), true);

		FString SerializedSummary;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&SerializedSummary);

		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("format"), TEXT("Vergil.LargeGraphBenchmarkSummary"));
		Writer->WriteValue(TEXT("version"), 1);
		Writer->WriteValue(TEXT("generatedUtc"), FDateTime::UtcNow().ToString(TEXT("%Y-%m-%dT%H:%M:%SZ")));
		Writer->WriteArrayStart(TEXT("scenarios"));
		for (const FVergilLargeGraphBenchmarkResult& Result : Results)
		{
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("name"), Result.Name);
			Writer->WriteValue(TEXT("segmentCount"), Result.SegmentCount);
			Writer->WriteValue(TEXT("commentCount"), Result.CommentCount);
			Writer->WriteValue(TEXT("documentNodeCount"), Result.DocumentNodeCount);
			Writer->WriteValue(TEXT("documentEdgeCount"), Result.DocumentEdgeCount);
			Writer->WriteValue(TEXT("appliedGraphNodeCount"), Result.AppliedGraphNodeCount);
			Writer->WriteValue(TEXT("maxPlanSeconds"), Result.MaxPlanSeconds);
			Writer->WriteValue(TEXT("maxApplySeconds"), Result.MaxApplySeconds);

			Writer->WriteObjectStart(TEXT("plan"));
			Writer->WriteValue(TEXT("succeeded"), Result.Plan.bSucceeded);
			Writer->WriteValue(TEXT("elapsedSeconds"), Result.Plan.ElapsedSeconds);
			Writer->WriteValue(TEXT("diagnosticCount"), Result.Plan.DiagnosticCount);
			Writer->WriteValue(TEXT("commandCount"), Result.Plan.CommandCount);
			Writer->WriteValue(TEXT("executedCommandCount"), Result.Plan.ExecutedCommandCount);
			Writer->WriteValue(TEXT("commandPlanFingerprint"), Result.Plan.CommandPlanFingerprint);
			Writer->WriteObjectEnd();

			Writer->WriteObjectStart(TEXT("apply"));
			Writer->WriteValue(TEXT("succeeded"), Result.Apply.bSucceeded);
			Writer->WriteValue(TEXT("elapsedSeconds"), Result.Apply.ElapsedSeconds);
			Writer->WriteValue(TEXT("diagnosticCount"), Result.Apply.DiagnosticCount);
			Writer->WriteValue(TEXT("commandCount"), Result.Apply.CommandCount);
			Writer->WriteValue(TEXT("executedCommandCount"), Result.Apply.ExecutedCommandCount);
			Writer->WriteValue(TEXT("commandPlanFingerprint"), Result.Apply.CommandPlanFingerprint);
			Writer->WriteObjectEnd();

			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
		Writer->Close();

		FFileHelper::SaveStringToFile(SerializedSummary, *OutSummaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FVergilCompileResult RunPlanResult(
		UVergilEditorSubsystem* const EditorSubsystem,
		UBlueprint* const Blueprint,
		const FVergilGraphDocument& Document,
		double& OutElapsedSeconds)
	{
		const double StartTime = FPlatformTime::Seconds();
		FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, true, true, false);
		OutElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		return Result;
	}

	FVergilLargeGraphBenchmarkPhaseResult RunApplyPhase(
		UVergilEditorSubsystem* const EditorSubsystem,
		UBlueprint* const Blueprint,
		const TArray<FVergilCompilerCommand>& Commands)
	{
		const double StartTime = FPlatformTime::Seconds();
		const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, Commands);
		const double EndTime = FPlatformTime::Seconds();

		FVergilLargeGraphBenchmarkPhaseResult Phase;
		Phase.bSucceeded = Result.bSucceeded && Result.bApplied;
		Phase.ElapsedSeconds = EndTime - StartTime;
		Phase.DiagnosticCount = Result.Diagnostics.Num();
		Phase.CommandCount = Result.Commands.Num();
		Phase.ExecutedCommandCount = Result.ExecutedCommandCount;
		Phase.CommandPlanFingerprint = Result.Statistics.CommandPlanFingerprint;
		return Phase;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilLargeGraphBenchmarkTest,
	"Vergil.Scaffold.LargeGraphBenchmark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilLargeGraphBenchmarkTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	const TArray<FVergilLargeGraphBenchmarkScenario> Scenarios =
	{
		MakeBenchmarkScenario(TEXT("LinearBoolToggleChain_192"), 192, 24, 15.0, 25.0),
		MakeBenchmarkScenario(TEXT("LinearBoolToggleChain_384"), 384, 24, 25.0, 40.0)
	};

	TArray<FVergilLargeGraphBenchmarkResult> Results;
	Results.Reserve(Scenarios.Num());

	bool bAllSucceeded = true;

	for (const FVergilLargeGraphBenchmarkScenario& Scenario : Scenarios)
	{
		int32 CommentCount = 0;
		const FVergilGraphDocument Document = BuildBenchmarkLargeGraphDocument(Scenario, CommentCount);

		FVergilLargeGraphBenchmarkResult& BenchmarkResult = Results.AddDefaulted_GetRef();
		BenchmarkResult.Name = Scenario.Name;
		BenchmarkResult.SegmentCount = Scenario.SegmentCount;
		BenchmarkResult.CommentCount = CommentCount;
		BenchmarkResult.DocumentNodeCount = Document.Nodes.Num();
		BenchmarkResult.DocumentEdgeCount = Document.Edges.Num();
		BenchmarkResult.MaxPlanSeconds = Scenario.MaxPlanSeconds;
		BenchmarkResult.MaxApplySeconds = Scenario.MaxApplySeconds;

		TStrongObjectPtr<UBlueprint> PlanningBlueprint = MakeBenchmarkTransientBlueprint();
		TestNotNull(*FString::Printf(TEXT("%s should create a transient planning Blueprint."), *Scenario.Name), PlanningBlueprint.Get());
		if (!PlanningBlueprint.IsValid())
		{
			bAllSucceeded = false;
			continue;
		}

		double PlanElapsedSeconds = 0.0;
		const FVergilCompileResult PlanResult = RunPlanResult(EditorSubsystem, PlanningBlueprint.Get(), Document, PlanElapsedSeconds);
		BenchmarkResult.Plan.bSucceeded = PlanResult.bSucceeded && !PlanResult.bApplied;
		BenchmarkResult.Plan.ElapsedSeconds = PlanElapsedSeconds;
		BenchmarkResult.Plan.DiagnosticCount = PlanResult.Diagnostics.Num();
		BenchmarkResult.Plan.CommandCount = PlanResult.Commands.Num();
		BenchmarkResult.Plan.ExecutedCommandCount = PlanResult.ExecutedCommandCount;
		BenchmarkResult.Plan.CommandPlanFingerprint = PlanResult.Statistics.CommandPlanFingerprint;

		const FString PlanLabel = FString::Printf(TEXT("%s plan benchmark"), *Scenario.Name);
		TestTrue(*FString::Printf(TEXT("%s should succeed."), *PlanLabel), BenchmarkResult.Plan.bSucceeded);
		TestEqual(*FString::Printf(TEXT("%s should emit zero diagnostics."), *PlanLabel), BenchmarkResult.Plan.DiagnosticCount, 0);
		TestTrue(*FString::Printf(TEXT("%s should emit a command plan larger than the document node count."), *PlanLabel), BenchmarkResult.Plan.CommandCount > BenchmarkResult.DocumentNodeCount);
		TestTrue(*FString::Printf(TEXT("%s should remain within %.2fs."), *PlanLabel, Scenario.MaxPlanSeconds), BenchmarkResult.Plan.ElapsedSeconds <= Scenario.MaxPlanSeconds);
		if (!BenchmarkResult.Plan.bSucceeded
			|| BenchmarkResult.Plan.DiagnosticCount != 0
			|| BenchmarkResult.Plan.CommandCount <= BenchmarkResult.DocumentNodeCount
			|| BenchmarkResult.Plan.ElapsedSeconds > Scenario.MaxPlanSeconds)
		{
			bAllSucceeded = false;
			continue;
		}

		TStrongObjectPtr<UBlueprint> ApplyBlueprint = MakeBenchmarkTransientBlueprint();
		TestNotNull(*FString::Printf(TEXT("%s should create a transient apply Blueprint."), *Scenario.Name), ApplyBlueprint.Get());
		if (!ApplyBlueprint.IsValid())
		{
			bAllSucceeded = false;
			continue;
		}

		BenchmarkResult.Apply = RunApplyPhase(EditorSubsystem, ApplyBlueprint.Get(), PlanResult.Commands);
		BenchmarkResult.AppliedGraphNodeCount = CountBenchmarkEventGraphNodes(ApplyBlueprint.Get());

		const FString ApplyLabel = FString::Printf(TEXT("%s apply benchmark"), *Scenario.Name);
		TestTrue(*FString::Printf(TEXT("%s should succeed."), *ApplyLabel), BenchmarkResult.Apply.bSucceeded);
		TestEqual(*FString::Printf(TEXT("%s should emit zero diagnostics."), *ApplyLabel), BenchmarkResult.Apply.DiagnosticCount, 0);
		TestEqual(*FString::Printf(TEXT("%s should execute the full planned command count."), *ApplyLabel), BenchmarkResult.Apply.ExecutedCommandCount, BenchmarkResult.Plan.CommandCount);
		TestTrue(*FString::Printf(TEXT("%s should materialize at least the authored node count in the event graph."), *ApplyLabel), BenchmarkResult.AppliedGraphNodeCount >= BenchmarkResult.DocumentNodeCount);
		TestTrue(*FString::Printf(TEXT("%s should remain within %.2fs."), *ApplyLabel, Scenario.MaxApplySeconds), BenchmarkResult.Apply.ElapsedSeconds <= Scenario.MaxApplySeconds);
		if (!BenchmarkResult.Apply.bSucceeded
			|| BenchmarkResult.Apply.DiagnosticCount != 0
			|| BenchmarkResult.Apply.ExecutedCommandCount != BenchmarkResult.Plan.CommandCount
			|| BenchmarkResult.AppliedGraphNodeCount < BenchmarkResult.DocumentNodeCount
			|| BenchmarkResult.Apply.ElapsedSeconds > Scenario.MaxApplySeconds)
		{
			bAllSucceeded = false;
		}

		AddInfo(FString::Printf(
			TEXT("Large-graph benchmark %s: nodes=%d edges=%d comments=%d plan=%0.3fs (%d commands) apply=%0.3fs (%d executed commands) eventGraphNodes=%d"),
			*Scenario.Name,
			BenchmarkResult.DocumentNodeCount,
			BenchmarkResult.DocumentEdgeCount,
			BenchmarkResult.CommentCount,
			BenchmarkResult.Plan.ElapsedSeconds,
			BenchmarkResult.Plan.CommandCount,
			BenchmarkResult.Apply.ElapsedSeconds,
			BenchmarkResult.Apply.ExecutedCommandCount,
			BenchmarkResult.AppliedGraphNodeCount));
	}

	FString SummaryPath;
	WriteBenchmarkSummaryFile(Results, SummaryPath);
	AddInfo(FString::Printf(TEXT("Large-graph benchmark summary written to %s"), *SummaryPath));

	return bAllSucceeded;
}
