#include "VergilGraphDocument.h"

namespace
{
	void AddDiagnostic(
		TArray<FVergilDiagnostic>* OutDiagnostics,
		const EVergilDiagnosticSeverity Severity,
		const FName Code,
		const FString& Message,
		const FGuid& SourceId = FGuid())
	{
		if (OutDiagnostics == nullptr)
		{
			return;
		}

		OutDiagnostics->Add(FVergilDiagnostic::Make(Severity, Code, Message, SourceId));
	}
}

bool FVergilGraphDocument::IsStructurallyValid(TArray<FVergilDiagnostic>* OutDiagnostics) const
{
	bool bIsValid = true;

	if (SchemaVersion <= 0)
	{
		bIsValid = false;
		AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("InvalidSchemaVersion"), TEXT("SchemaVersion must be greater than zero."));
	}

	TSet<FGuid> NodeIds;
	TSet<FGuid> PinIds;
	TSet<FName> DispatcherNames;

	for (const FVergilDispatcherDefinition& Dispatcher : Dispatchers)
	{
		if (Dispatcher.Name.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("DispatcherNameMissing"), TEXT("Every dispatcher must declare a name."));
			continue;
		}

		if (DispatcherNames.Contains(Dispatcher.Name))
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("DispatcherNameDuplicate"), FString::Printf(TEXT("Duplicate dispatcher name '%s'."), *Dispatcher.Name.ToString()));
			continue;
		}

		DispatcherNames.Add(Dispatcher.Name);

		TSet<FName> ParameterNames;
		for (const FVergilDispatcherParameter& Parameter : Dispatcher.Parameters)
		{
			if (Parameter.Name.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("DispatcherParameterNameMissing"), FString::Printf(TEXT("Dispatcher '%s' contains a parameter without a name."), *Dispatcher.Name.ToString()));
				continue;
			}

			if (ParameterNames.Contains(Parameter.Name))
			{
				bIsValid = false;
				AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("DispatcherParameterNameDuplicate"), FString::Printf(TEXT("Dispatcher '%s' contains duplicate parameter '%s'."), *Dispatcher.Name.ToString(), *Parameter.Name.ToString()));
				continue;
			}

			ParameterNames.Add(Parameter.Name);

			if (Parameter.PinCategory.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("DispatcherParameterCategoryMissing"), FString::Printf(TEXT("Dispatcher '%s' parameter '%s' must declare a pin category."), *Dispatcher.Name.ToString(), *Parameter.Name.ToString()));
			}
		}
	}

	for (const FVergilGraphNode& Node : Nodes)
	{
		if (!Node.Id.IsValid())
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("NodeIdMissing"), TEXT("Every node must have a valid GUID."), Node.Id);
		}
		else if (NodeIds.Contains(Node.Id))
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("NodeIdDuplicate"), FString::Printf(TEXT("Duplicate node id %s."), *Node.Id.ToString()), Node.Id);
		}
		else
		{
			NodeIds.Add(Node.Id);
		}

		if (Node.Descriptor.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("NodeDescriptorMissing"), TEXT("Every node must declare a descriptor."), Node.Id);
		}

		for (const FVergilGraphPin& Pin : Node.Pins)
		{
			if (!Pin.Id.IsValid())
			{
				bIsValid = false;
				AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("PinIdMissing"), TEXT("Every pin must have a valid GUID."), Node.Id);
			}
			else if (PinIds.Contains(Pin.Id))
			{
				bIsValid = false;
				AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("PinIdDuplicate"), FString::Printf(TEXT("Duplicate pin id %s."), *Pin.Id.ToString()), Node.Id);
			}
			else
			{
				PinIds.Add(Pin.Id);
			}
		}
	}

	for (const FVergilGraphEdge& Edge : Edges)
	{
		const bool bHasValidSourceNode = NodeIds.Contains(Edge.SourceNodeId);
		const bool bHasValidTargetNode = NodeIds.Contains(Edge.TargetNodeId);
		const bool bHasValidSourcePin = PinIds.Contains(Edge.SourcePinId);
		const bool bHasValidTargetPin = PinIds.Contains(Edge.TargetPinId);

		if (!bHasValidSourceNode || !bHasValidTargetNode || !bHasValidSourcePin || !bHasValidTargetPin)
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("EdgeReferenceInvalid"), TEXT("Edges must reference existing node and pin identifiers."), Edge.Id);
		}
	}

	if (SchemaVersion > Vergil::SchemaVersion)
	{
		AddDiagnostic(
			OutDiagnostics,
			EVergilDiagnosticSeverity::Warning,
			TEXT("SchemaVersionFuture"),
			FString::Printf(TEXT("Document schema %d is newer than compiler schema %d."), SchemaVersion, Vergil::SchemaVersion));
	}

	return bIsValid;
}
