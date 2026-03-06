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

	bool IsSupportedTypeCategory(const FName PinCategory)
	{
		const FString Category = PinCategory.ToString().ToLower();
		return Category == TEXT("bool")
			|| Category == TEXT("int")
			|| Category == TEXT("float")
			|| Category == TEXT("double")
			|| Category == TEXT("string")
			|| Category == TEXT("name")
			|| Category == TEXT("text")
			|| Category == TEXT("enum")
			|| Category == TEXT("object")
			|| Category == TEXT("class")
			|| Category == TEXT("struct");
	}

	bool TypeCategoryRequiresObjectPath(const FName PinCategory)
	{
		const FString Category = PinCategory.ToString().ToLower();
		return Category == TEXT("enum")
			|| Category == TEXT("object")
			|| Category == TEXT("class")
			|| Category == TEXT("struct");
	}

	bool ValidateTypeReference(
		const FName PinCategory,
		const FString& ObjectPath,
		const FString& ContextLabel,
		const FName MissingCategoryCode,
		const FName UnsupportedCategoryCode,
		const FName MissingObjectPathCode,
		TArray<FVergilDiagnostic>* OutDiagnostics)
	{
		if (PinCategory.IsNone())
		{
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				MissingCategoryCode,
				FString::Printf(TEXT("%s must declare a type category."), *ContextLabel));
			return false;
		}

		if (!IsSupportedTypeCategory(PinCategory))
		{
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				UnsupportedCategoryCode,
				FString::Printf(TEXT("%s declares unsupported type category '%s'."), *ContextLabel, *PinCategory.ToString()));
			return false;
		}

		if (TypeCategoryRequiresObjectPath(PinCategory) && ObjectPath.IsEmpty())
		{
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				MissingObjectPathCode,
				FString::Printf(TEXT("%s type category '%s' requires an object path."), *ContextLabel, *PinCategory.ToString()));
			return false;
		}

		return true;
	}

	bool ValidateVariableTypeReference(
		const FVergilVariableTypeReference& Type,
		const FString& ContextLabel,
		TArray<FVergilDiagnostic>* OutDiagnostics)
	{
		bool bIsValid = ValidateTypeReference(
			Type.PinCategory,
			Type.ObjectPath,
			ContextLabel,
			TEXT("VariableTypeCategoryMissing"),
			TEXT("VariableTypeCategoryUnsupported"),
			TEXT("VariableTypeObjectPathMissing"),
			OutDiagnostics);

		if (Type.ContainerType == EVergilVariableContainerType::Map)
		{
			if (Type.ValuePinCategory.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("VariableMapValueCategoryMissing"),
					FString::Printf(TEXT("%s map definitions must declare a value type category."), *ContextLabel));
			}
			else
			{
				bIsValid &= ValidateTypeReference(
					Type.ValuePinCategory,
					Type.ValueObjectPath,
					FString::Printf(TEXT("%s value type"), *ContextLabel),
					TEXT("VariableTypeCategoryMissing"),
					TEXT("VariableTypeCategoryUnsupported"),
					TEXT("VariableTypeObjectPathMissing"),
					OutDiagnostics);
			}
		}
		else if (!Type.ValuePinCategory.IsNone()
			|| !Type.ValuePinSubCategory.IsNone()
			|| !Type.ValueObjectPath.IsEmpty())
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableValueTypeUnexpected"),
				FString::Printf(TEXT("%s may only declare value-type fields for map containers."), *ContextLabel));
		}

		return bIsValid;
	}

	bool ValidateFunctionParameterTypeReference(
		const FVergilVariableTypeReference& Type,
		const FString& ContextLabel,
		TArray<FVergilDiagnostic>* OutDiagnostics)
	{
		bool bIsValid = ValidateTypeReference(
			Type.PinCategory,
			Type.ObjectPath,
			ContextLabel,
			TEXT("FunctionParameterTypeCategoryMissing"),
			TEXT("FunctionParameterTypeCategoryUnsupported"),
			TEXT("FunctionParameterTypeObjectPathMissing"),
			OutDiagnostics);

		if (Type.ContainerType == EVergilVariableContainerType::Map)
		{
			if (Type.ValuePinCategory.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionParameterMapValueCategoryMissing"),
					FString::Printf(TEXT("%s map definitions must declare a value type category."), *ContextLabel));
			}
			else
			{
				bIsValid &= ValidateTypeReference(
					Type.ValuePinCategory,
					Type.ValueObjectPath,
					FString::Printf(TEXT("%s value type"), *ContextLabel),
					TEXT("FunctionParameterTypeCategoryMissing"),
					TEXT("FunctionParameterTypeCategoryUnsupported"),
					TEXT("FunctionParameterTypeObjectPathMissing"),
					OutDiagnostics);
			}
		}
		else if (!Type.ValuePinCategory.IsNone()
			|| !Type.ValuePinSubCategory.IsNone()
			|| !Type.ValueObjectPath.IsEmpty())
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionParameterValueTypeUnexpected"),
				FString::Printf(TEXT("%s may only declare value-type fields for map containers."), *ContextLabel));
		}

		return bIsValid;
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
	TSet<FName> VariableNames;
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

	for (const FVergilVariableDefinition& Variable : Variables)
	{
		const FString VariableLabel = Variable.Name.IsNone()
			? FString(TEXT("Variable"))
			: FString::Printf(TEXT("Variable '%s'"), *Variable.Name.ToString());

		if (Variable.Name.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("VariableNameMissing"), TEXT("Every variable must declare a name."));
			continue;
		}

		if (VariableNames.Contains(Variable.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableNameDuplicate"),
				FString::Printf(TEXT("Duplicate variable name '%s'."), *Variable.Name.ToString()));
			continue;
		}

		if (DispatcherNames.Contains(Variable.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableNameConflictsWithDispatcher"),
				FString::Printf(TEXT("Variable '%s' conflicts with a dispatcher of the same name."), *Variable.Name.ToString()));
			continue;
		}

		VariableNames.Add(Variable.Name);

		bIsValid &= ValidateVariableTypeReference(Variable.Type, VariableLabel, OutDiagnostics);

		if (Variable.Flags.bExposeOnSpawn && !Variable.Flags.bInstanceEditable)
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableExposeOnSpawnRequiresInstanceEditable"),
				FString::Printf(TEXT("%s cannot enable ExposeOnSpawn unless it is also instance editable."), *VariableLabel));
		}
	}

	TSet<FName> FunctionNames;
	for (const FVergilFunctionDefinition& Function : Functions)
	{
		const FString FunctionLabel = Function.Name.IsNone()
			? FString(TEXT("Function"))
			: FString::Printf(TEXT("Function '%s'"), *Function.Name.ToString());

		if (Function.Name.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("FunctionNameMissing"), TEXT("Every function must declare a name."));
			continue;
		}

		if (FunctionNames.Contains(Function.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionNameDuplicate"),
				FString::Printf(TEXT("Duplicate function name '%s'."), *Function.Name.ToString()));
			continue;
		}

		if (VariableNames.Contains(Function.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionNameConflictsWithVariable"),
				FString::Printf(TEXT("Function '%s' conflicts with a variable of the same name."), *Function.Name.ToString()));
			continue;
		}

		if (DispatcherNames.Contains(Function.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionNameConflictsWithDispatcher"),
				FString::Printf(TEXT("Function '%s' conflicts with a dispatcher of the same name."), *Function.Name.ToString()));
			continue;
		}

		FunctionNames.Add(Function.Name);

		TSet<FName> SignatureNames;
		for (const FVergilFunctionParameterDefinition& Input : Function.Inputs)
		{
			const FString InputLabel = Input.Name.IsNone()
				? FString::Printf(TEXT("%s input"), *FunctionLabel)
				: FString::Printf(TEXT("%s input '%s'"), *FunctionLabel, *Input.Name.ToString());

			if (Input.Name.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionInputNameMissing"),
					FString::Printf(TEXT("%s contains an input without a name."), *FunctionLabel));
				continue;
			}

			if (SignatureNames.Contains(Input.Name))
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionParameterNameDuplicate"),
					FString::Printf(TEXT("%s contains duplicate signature member '%s'."), *FunctionLabel, *Input.Name.ToString()));
				continue;
			}

			SignatureNames.Add(Input.Name);
			bIsValid &= ValidateFunctionParameterTypeReference(Input.Type, InputLabel, OutDiagnostics);
		}

		for (const FVergilFunctionParameterDefinition& Output : Function.Outputs)
		{
			const FString OutputLabel = Output.Name.IsNone()
				? FString::Printf(TEXT("%s output"), *FunctionLabel)
				: FString::Printf(TEXT("%s output '%s'"), *FunctionLabel, *Output.Name.ToString());

			if (Output.Name.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionOutputNameMissing"),
					FString::Printf(TEXT("%s contains an output without a name."), *FunctionLabel));
				continue;
			}

			if (SignatureNames.Contains(Output.Name))
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionParameterNameDuplicate"),
					FString::Printf(TEXT("%s contains duplicate signature member '%s'."), *FunctionLabel, *Output.Name.ToString()));
				continue;
			}

			SignatureNames.Add(Output.Name);
			bIsValid &= ValidateFunctionParameterTypeReference(Output.Type, OutputLabel, OutDiagnostics);
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
