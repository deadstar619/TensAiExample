#include "VergilReflectionInfo.h"

#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/FieldIterator.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	inline constexpr TCHAR ReflectionSymbolFormatName[] = TEXT("Vergil.ReflectionSymbol");
	inline constexpr int32 ReflectionSymbolFormatVersion = 1;
	inline constexpr TCHAR ReflectionDiscoveryFormatName[] = TEXT("Vergil.ReflectionDiscovery");
	inline constexpr int32 ReflectionDiscoveryFormatVersion = 1;

	const TCHAR* LexReflectionSymbolKind(const EVergilReflectionSymbolKind Kind)
	{
		switch (Kind)
		{
		case EVergilReflectionSymbolKind::Class: return TEXT("Class");
		case EVergilReflectionSymbolKind::Struct: return TEXT("Struct");
		case EVergilReflectionSymbolKind::Enum: return TEXT("Enum");
		default: return TEXT("None");
		}
	}

	const TCHAR* LexReflectionParameterDirection(const EVergilReflectionParameterDirection Direction)
	{
		switch (Direction)
		{
		case EVergilReflectionParameterDirection::Input: return TEXT("Input");
		case EVergilReflectionParameterDirection::Output: return TEXT("Output");
		case EVergilReflectionParameterDirection::Return: return TEXT("Return");
		default: return TEXT("Input");
		}
	}

	FString EscapeDisplayValue(const FString& Value)
	{
		FString EscapedValue = Value;
		EscapedValue.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		EscapedValue.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		return EscapedValue;
	}

	bool IsDiscoverableObject(const UObject* const Object)
	{
		if (Object == nullptr)
		{
			return false;
		}

		const FString Name = Object->GetName();
		if (Name.StartsWith(TEXT("SKEL_"))
			|| Name.StartsWith(TEXT("REINST_"))
			|| Name.StartsWith(TEXT("TRASHCLASS_"))
			|| Name.StartsWith(TEXT("PLACEHOLDER-CLASS_")))
		{
			return false;
		}

		return !Object->GetPathName().StartsWith(TEXT("/Engine/Transient"));
	}

	bool IsDiscoverableClass(const UClass* const Class)
	{
		return IsDiscoverableObject(Class)
			&& Class != nullptr
			&& !Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists);
	}

	bool ShouldIncludeClassFunction(const UFunction& Function)
	{
		return !Function.HasAnyFunctionFlags(FUNC_Delegate)
			&& Function.HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_BlueprintPure);
	}

	bool ShouldIncludeClassProperty(const FProperty& Property)
	{
		return Property.HasAnyPropertyFlags(CPF_BlueprintVisible);
	}

	bool ShouldIncludeStructProperty(const FProperty& Property)
	{
		return !Property.HasAnyPropertyFlags(CPF_Deprecated);
	}

	FString TrimReflectionQuery(const FString& Query)
	{
		return Query.TrimStartAndEnd();
	}

	FString BuildPackageObjectPath(const FString& Reference)
	{
		const FString TrimmedReference = TrimReflectionQuery(Reference);
		if (TrimmedReference.IsEmpty())
		{
			return FString();
		}

		if (TrimmedReference.Contains(TEXT(".")))
		{
			return TrimmedReference;
		}

		const FString AssetName = FPackageName::GetLongPackageAssetName(TrimmedReference);
		return AssetName.IsEmpty() ? FString() : FString::Printf(TEXT("%s.%s"), *TrimmedReference, *AssetName);
	}

	UBlueprint* ResolveBlueprintAsset(const FString& Reference)
	{
		const FString ObjectPath = BuildPackageObjectPath(Reference);
		if (ObjectPath.IsEmpty())
		{
			return nullptr;
		}

		if (UBlueprint* const ExistingBlueprint = FindObject<UBlueprint>(nullptr, *ObjectPath))
		{
			return ExistingBlueprint;
		}

		return LoadObject<UBlueprint>(nullptr, *ObjectPath);
	}

	UClass* ResolveClassByPath(const FString& Query)
	{
		const FString TrimmedQuery = TrimReflectionQuery(Query);
		if (TrimmedQuery.IsEmpty())
		{
			return nullptr;
		}

		if (!TrimmedQuery.Contains(TEXT("/")) && !TrimmedQuery.Contains(TEXT(".")))
		{
			return nullptr;
		}

		if (UClass* const ExistingClass = FindObject<UClass>(nullptr, *TrimmedQuery))
		{
			return ExistingClass;
		}

		const bool bIsScriptReference = TrimmedQuery.StartsWith(TEXT("/Script/"));
		if (bIsScriptReference)
		{
			return nullptr;
		}

		if (UClass* const LoadedClass = LoadObject<UClass>(nullptr, *TrimmedQuery))
		{
			return LoadedClass;
		}

		const FString ObjectPath = BuildPackageObjectPath(TrimmedQuery);
		if (!ObjectPath.IsEmpty())
		{
			const FString GeneratedClassPath = ObjectPath.EndsWith(TEXT("_C")) ? ObjectPath : ObjectPath + TEXT("_C");
			if (UClass* const ExistingGeneratedClass = FindObject<UClass>(nullptr, *GeneratedClassPath))
			{
				return ExistingGeneratedClass;
			}

			if (UClass* const LoadedGeneratedClass = LoadObject<UClass>(nullptr, *GeneratedClassPath))
			{
				return LoadedGeneratedClass;
			}
		}

		if (UBlueprint* const BlueprintAsset = ResolveBlueprintAsset(TrimmedQuery))
		{
			return BlueprintAsset->GeneratedClass;
		}

		return nullptr;
	}

	UEnum* ResolveEnumByPath(const FString& Query)
	{
		const FString TrimmedQuery = TrimReflectionQuery(Query);
		if (TrimmedQuery.IsEmpty() || (!TrimmedQuery.Contains(TEXT("/")) && !TrimmedQuery.Contains(TEXT("."))))
		{
			return nullptr;
		}

		if (UEnum* const ExistingEnum = FindObject<UEnum>(nullptr, *TrimmedQuery))
		{
			return ExistingEnum;
		}

		return TrimmedQuery.StartsWith(TEXT("/Script/")) ? nullptr : LoadObject<UEnum>(nullptr, *TrimmedQuery);
	}

	UScriptStruct* ResolveStructByPath(const FString& Query)
	{
		const FString TrimmedQuery = TrimReflectionQuery(Query);
		if (TrimmedQuery.IsEmpty() || (!TrimmedQuery.Contains(TEXT("/")) && !TrimmedQuery.Contains(TEXT("."))))
		{
			return nullptr;
		}

		if (UScriptStruct* const ExistingStruct = FindObject<UScriptStruct>(nullptr, *TrimmedQuery))
		{
			return ExistingStruct;
		}

		return TrimmedQuery.StartsWith(TEXT("/Script/")) ? nullptr : LoadObject<UScriptStruct>(nullptr, *TrimmedQuery);
	}

	FString GetPropertyTypeObjectPath(const FProperty& Property)
	{
		if (const FObjectPropertyBase* const ObjectProperty = CastField<FObjectPropertyBase>(&Property))
		{
			return ObjectProperty->PropertyClass != nullptr ? ObjectProperty->PropertyClass->GetPathName() : FString();
		}
		if (const FClassProperty* const ClassProperty = CastField<FClassProperty>(&Property))
		{
			return ClassProperty->MetaClass != nullptr ? ClassProperty->MetaClass->GetPathName() : FString();
		}
		if (const FSoftObjectProperty* const SoftObjectProperty = CastField<FSoftObjectProperty>(&Property))
		{
			return SoftObjectProperty->PropertyClass != nullptr ? SoftObjectProperty->PropertyClass->GetPathName() : FString();
		}
		if (const FSoftClassProperty* const SoftClassProperty = CastField<FSoftClassProperty>(&Property))
		{
			return SoftClassProperty->MetaClass != nullptr ? SoftClassProperty->MetaClass->GetPathName() : FString();
		}
		if (const FStructProperty* const StructProperty = CastField<FStructProperty>(&Property))
		{
			return StructProperty->Struct != nullptr ? StructProperty->Struct->GetPathName() : FString();
		}
		if (const FEnumProperty* const EnumProperty = CastField<FEnumProperty>(&Property))
		{
			return EnumProperty->GetEnum() != nullptr ? EnumProperty->GetEnum()->GetPathName() : FString();
		}
		if (const FByteProperty* const ByteProperty = CastField<FByteProperty>(&Property))
		{
			return ByteProperty->Enum != nullptr ? ByteProperty->Enum->GetPathName() : FString();
		}
		return FString();
	}

	FVergilReflectionParameterInfo BuildParameterInfo(const FProperty& Property)
	{
		FVergilReflectionParameterInfo ParameterInfo;
		ParameterInfo.Name = Property.GetName();
		ParameterInfo.Type = Property.GetCPPType();
		ParameterInfo.TypeObjectPath = GetPropertyTypeObjectPath(Property);
		ParameterInfo.bIsConst = Property.HasAnyPropertyFlags(CPF_ConstParm);
		ParameterInfo.bIsReference = Property.HasAnyPropertyFlags(CPF_ReferenceParm | CPF_OutParm);
		ParameterInfo.Direction = Property.HasAnyPropertyFlags(CPF_ReturnParm)
			? EVergilReflectionParameterDirection::Return
			: (Property.HasAnyPropertyFlags(CPF_OutParm) && !Property.HasAnyPropertyFlags(CPF_ConstParm))
				? EVergilReflectionParameterDirection::Output
				: EVergilReflectionParameterDirection::Input;
		return ParameterInfo;
	}

	FVergilReflectionPropertyInfo BuildPropertyInfo(const FProperty& Property)
	{
		FVergilReflectionPropertyInfo PropertyInfo;
		PropertyInfo.Name = Property.GetName();
		PropertyInfo.OwnerPath = Property.GetOwnerStruct() != nullptr ? Property.GetOwnerStruct()->GetPathName() : FString();
		PropertyInfo.Type = Property.GetCPPType();
		PropertyInfo.TypeObjectPath = GetPropertyTypeObjectPath(Property);
		PropertyInfo.bBlueprintVisible = Property.HasAnyPropertyFlags(CPF_BlueprintVisible);
		PropertyInfo.bBlueprintReadOnly = Property.HasAnyPropertyFlags(CPF_BlueprintReadOnly);
		PropertyInfo.bEditable = Property.HasAnyPropertyFlags(CPF_Edit);
		PropertyInfo.bExposeOnSpawn = Property.HasMetaData(TEXT("ExposeOnSpawn"));
		return PropertyInfo;
	}

	FVergilReflectionFunctionInfo BuildFunctionInfo(const UFunction& Function)
	{
		FVergilReflectionFunctionInfo FunctionInfo;
		FunctionInfo.Name = Function.GetName();
		FunctionInfo.OwnerPath = Function.GetOwnerStruct() != nullptr ? Function.GetOwnerStruct()->GetPathName() : FString();
		FunctionInfo.bBlueprintCallable = Function.HasAnyFunctionFlags(FUNC_BlueprintCallable);
		FunctionInfo.bBlueprintPure = Function.HasAnyFunctionFlags(FUNC_BlueprintPure);
		FunctionInfo.bConst = Function.HasAnyFunctionFlags(FUNC_Const);
		FunctionInfo.bStatic = Function.HasAnyFunctionFlags(FUNC_Static);
		FunctionInfo.bLatent = Function.HasMetaData(TEXT("Latent"));

		for (TFieldIterator<FProperty> PropertyIt(&Function); PropertyIt; ++PropertyIt)
		{
			const FProperty* const Property = *PropertyIt;
			if (Property != nullptr && Property->HasAnyPropertyFlags(CPF_Parm))
			{
				FunctionInfo.Parameters.Add(BuildParameterInfo(*Property));
			}
		}

		return FunctionInfo;
	}

	TArray<FString> GatherExactShortNameCandidates(const FString& Query)
	{
		TArray<FString> CandidatePaths;
		TSet<FString> SeenPaths;
		const FString TrimmedQuery = TrimReflectionQuery(Query);
		if (TrimmedQuery.IsEmpty())
		{
			return CandidatePaths;
		}

		auto AddPath = [&CandidatePaths, &SeenPaths](const UObject* const Object)
		{
			if (!IsDiscoverableObject(Object))
			{
				return;
			}

			const FString Path = Object->GetPathName();
			if (SeenPaths.Contains(Path))
			{
				return;
			}

			SeenPaths.Add(Path);
			CandidatePaths.Add(Path);
		};

		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (IsDiscoverableClass(*It) && It->GetName().Equals(TrimmedQuery, ESearchCase::IgnoreCase))
			{
				AddPath(*It);
			}
		}
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			if (IsDiscoverableObject(*It) && It->GetName().Equals(TrimmedQuery, ESearchCase::IgnoreCase))
			{
				AddPath(*It);
			}
		}
		for (TObjectIterator<UEnum> It; It; ++It)
		{
			if (IsDiscoverableObject(*It) && It->GetName().Equals(TrimmedQuery, ESearchCase::IgnoreCase))
			{
				AddPath(*It);
			}
		}

		CandidatePaths.Sort();
		return CandidatePaths;
	}

	void PopulateClassSymbolInfo(const UClass& Class, FVergilReflectionSymbolInfo& SymbolInfo)
	{
		SymbolInfo.bResolved = true;
		SymbolInfo.Kind = EVergilReflectionSymbolKind::Class;
		SymbolInfo.Name = Class.GetName();
		SymbolInfo.ResolvedPath = Class.GetPathName();
		SymbolInfo.SuperPath = Class.GetSuperClass() != nullptr ? Class.GetSuperClass()->GetPathName() : FString();

		TSet<FString> SeenFunctions;
		for (TFieldIterator<UFunction> FunctionIt(&Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
		{
			const UFunction* const Function = *FunctionIt;
			if (Function == nullptr || !ShouldIncludeClassFunction(*Function))
			{
				continue;
			}

			const FString Key = FString::Printf(
				TEXT("%s::%s"),
				Function->GetOwnerStruct() != nullptr ? *Function->GetOwnerStruct()->GetPathName() : TEXT("<none>"),
				*Function->GetName());
			if (SeenFunctions.Contains(Key))
			{
				continue;
			}

			SeenFunctions.Add(Key);
			SymbolInfo.Functions.Add(BuildFunctionInfo(*Function));
		}

		TSet<FString> SeenProperties;
		for (TFieldIterator<FProperty> PropertyIt(&Class, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* const Property = *PropertyIt;
			if (Property == nullptr || !ShouldIncludeClassProperty(*Property))
			{
				continue;
			}

			const FString Key = FString::Printf(
				TEXT("%s::%s"),
				Property->GetOwnerStruct() != nullptr ? *Property->GetOwnerStruct()->GetPathName() : TEXT("<none>"),
				*Property->GetName());
			if (SeenProperties.Contains(Key))
			{
				continue;
			}

			SeenProperties.Add(Key);
			SymbolInfo.Properties.Add(BuildPropertyInfo(*Property));
		}

		SymbolInfo.Functions.Sort([](const FVergilReflectionFunctionInfo& Left, const FVergilReflectionFunctionInfo& Right)
		{
			return Left.OwnerPath == Right.OwnerPath ? Left.Name < Right.Name : Left.OwnerPath < Right.OwnerPath;
		});
		SymbolInfo.Properties.Sort([](const FVergilReflectionPropertyInfo& Left, const FVergilReflectionPropertyInfo& Right)
		{
			return Left.OwnerPath == Right.OwnerPath ? Left.Name < Right.Name : Left.OwnerPath < Right.OwnerPath;
		});
	}

	void PopulateStructSymbolInfo(const UScriptStruct& Struct, FVergilReflectionSymbolInfo& SymbolInfo)
	{
		SymbolInfo.bResolved = true;
		SymbolInfo.Kind = EVergilReflectionSymbolKind::Struct;
		SymbolInfo.Name = Struct.GetName();
		SymbolInfo.ResolvedPath = Struct.GetPathName();
		SymbolInfo.SuperPath = Struct.GetSuperStruct() != nullptr ? Struct.GetSuperStruct()->GetPathName() : FString();

		TSet<FString> SeenProperties;
		for (TFieldIterator<FProperty> PropertyIt(&Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* const Property = *PropertyIt;
			if (Property == nullptr || !ShouldIncludeStructProperty(*Property))
			{
				continue;
			}

			const FString Key = FString::Printf(
				TEXT("%s::%s"),
				Property->GetOwnerStruct() != nullptr ? *Property->GetOwnerStruct()->GetPathName() : TEXT("<none>"),
				*Property->GetName());
			if (SeenProperties.Contains(Key))
			{
				continue;
			}

			SeenProperties.Add(Key);
			SymbolInfo.Properties.Add(BuildPropertyInfo(*Property));
		}

		SymbolInfo.Properties.Sort([](const FVergilReflectionPropertyInfo& Left, const FVergilReflectionPropertyInfo& Right)
		{
			return Left.OwnerPath == Right.OwnerPath ? Left.Name < Right.Name : Left.OwnerPath < Right.OwnerPath;
		});
	}

	void PopulateEnumSymbolInfo(const UEnum& Enum, FVergilReflectionSymbolInfo& SymbolInfo)
	{
		SymbolInfo.bResolved = true;
		SymbolInfo.Kind = EVergilReflectionSymbolKind::Enum;
		SymbolInfo.Name = Enum.GetName();
		SymbolInfo.ResolvedPath = Enum.GetPathName();

		for (int32 EnumIndex = 0; EnumIndex < Enum.NumEnums(); ++EnumIndex)
		{
			if (Enum.HasMetaData(TEXT("Hidden"), EnumIndex))
			{
				continue;
			}

			const FString EntryName = Enum.GetNameStringByIndex(EnumIndex);
			if (EntryName.EndsWith(TEXT("_MAX")))
			{
				continue;
			}

			FVergilReflectionEnumEntryInfo EntryInfo;
			EntryInfo.Name = EntryName;
			EntryInfo.Value = Enum.GetValueByIndex(EnumIndex);
			SymbolInfo.EnumEntries.Add(EntryInfo);
		}
	}

	template <typename WriterType>
	void WriteParameterJson(WriterType& Writer, const FVergilReflectionParameterInfo& ParameterInfo)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), ParameterInfo.Name);
		Writer->WriteValue(TEXT("type"), ParameterInfo.Type);
		Writer->WriteValue(TEXT("typeObjectPath"), ParameterInfo.TypeObjectPath);
		Writer->WriteValue(TEXT("direction"), LexReflectionParameterDirection(ParameterInfo.Direction));
		Writer->WriteValue(TEXT("const"), ParameterInfo.bIsConst);
		Writer->WriteValue(TEXT("reference"), ParameterInfo.bIsReference);
		Writer->WriteObjectEnd();
	}

	template <typename WriterType>
	void WriteFunctionJson(WriterType& Writer, const FVergilReflectionFunctionInfo& FunctionInfo)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), FunctionInfo.Name);
		Writer->WriteValue(TEXT("ownerPath"), FunctionInfo.OwnerPath);
		Writer->WriteValue(TEXT("blueprintCallable"), FunctionInfo.bBlueprintCallable);
		Writer->WriteValue(TEXT("blueprintPure"), FunctionInfo.bBlueprintPure);
		Writer->WriteValue(TEXT("const"), FunctionInfo.bConst);
		Writer->WriteValue(TEXT("static"), FunctionInfo.bStatic);
		Writer->WriteValue(TEXT("latent"), FunctionInfo.bLatent);
		Writer->WriteArrayStart(TEXT("parameters"));
		for (const FVergilReflectionParameterInfo& ParameterInfo : FunctionInfo.Parameters)
		{
			WriteParameterJson(Writer, ParameterInfo);
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
	}

	template <typename WriterType>
	void WritePropertyJson(WriterType& Writer, const FVergilReflectionPropertyInfo& PropertyInfo)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), PropertyInfo.Name);
		Writer->WriteValue(TEXT("ownerPath"), PropertyInfo.OwnerPath);
		Writer->WriteValue(TEXT("type"), PropertyInfo.Type);
		Writer->WriteValue(TEXT("typeObjectPath"), PropertyInfo.TypeObjectPath);
		Writer->WriteValue(TEXT("blueprintVisible"), PropertyInfo.bBlueprintVisible);
		Writer->WriteValue(TEXT("blueprintReadOnly"), PropertyInfo.bBlueprintReadOnly);
		Writer->WriteValue(TEXT("editable"), PropertyInfo.bEditable);
		Writer->WriteValue(TEXT("exposeOnSpawn"), PropertyInfo.bExposeOnSpawn);
		Writer->WriteObjectEnd();
	}

	template <typename WriterType>
	void WriteEnumEntryJson(WriterType& Writer, const FVergilReflectionEnumEntryInfo& EntryInfo)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), EntryInfo.Name);
		Writer->WriteValue(TEXT("value"), EntryInfo.Value);
		Writer->WriteObjectEnd();
	}

	template <typename WriterType>
	void WriteSearchResultJson(WriterType& Writer, const FVergilReflectionSearchResult& SearchResult)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("kind"), LexReflectionSymbolKind(SearchResult.Kind));
		Writer->WriteValue(TEXT("name"), SearchResult.Name);
		Writer->WriteValue(TEXT("resolvedPath"), SearchResult.ResolvedPath);
		Writer->WriteObjectEnd();
	}

	template <typename JsonPrintPolicy>
	FString SerializeReflectionSymbolInternal(const FVergilReflectionSymbolInfo& SymbolInfo)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, JsonPrintPolicy>> Writer = TJsonWriterFactory<TCHAR, JsonPrintPolicy>::Create(&Output);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("format"), ReflectionSymbolFormatName);
		Writer->WriteValue(TEXT("version"), ReflectionSymbolFormatVersion);
		Writer->WriteValue(TEXT("query"), SymbolInfo.Query);
		Writer->WriteValue(TEXT("resolved"), SymbolInfo.bResolved);
		Writer->WriteValue(TEXT("kind"), LexReflectionSymbolKind(SymbolInfo.Kind));
		Writer->WriteValue(TEXT("name"), SymbolInfo.Name);
		Writer->WriteValue(TEXT("resolvedPath"), SymbolInfo.ResolvedPath);
		Writer->WriteValue(TEXT("superPath"), SymbolInfo.SuperPath);
		Writer->WriteValue(TEXT("failureReason"), SymbolInfo.FailureReason);
		Writer->WriteArrayStart(TEXT("candidatePaths"));
		for (const FString& CandidatePath : SymbolInfo.CandidatePaths)
		{
			Writer->WriteValue(CandidatePath);
		}
		Writer->WriteArrayEnd();
		Writer->WriteArrayStart(TEXT("functions"));
		for (const FVergilReflectionFunctionInfo& FunctionInfo : SymbolInfo.Functions)
		{
			WriteFunctionJson(Writer, FunctionInfo);
		}
		Writer->WriteArrayEnd();
		Writer->WriteArrayStart(TEXT("properties"));
		for (const FVergilReflectionPropertyInfo& PropertyInfo : SymbolInfo.Properties)
		{
			WritePropertyJson(Writer, PropertyInfo);
		}
		Writer->WriteArrayEnd();
		Writer->WriteArrayStart(TEXT("enumEntries"));
		for (const FVergilReflectionEnumEntryInfo& EntryInfo : SymbolInfo.EnumEntries)
		{
			WriteEnumEntryJson(Writer, EntryInfo);
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
		Writer->Close();
		return Output;
	}

	template <typename JsonPrintPolicy>
	FString SerializeReflectionDiscoveryInternal(const FVergilReflectionDiscoveryResults& DiscoveryResults)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, JsonPrintPolicy>> Writer = TJsonWriterFactory<TCHAR, JsonPrintPolicy>::Create(&Output);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("format"), ReflectionDiscoveryFormatName);
		Writer->WriteValue(TEXT("version"), ReflectionDiscoveryFormatVersion);
		Writer->WriteValue(TEXT("query"), DiscoveryResults.Query);
		Writer->WriteArrayStart(TEXT("matches"));
		for (const FVergilReflectionSearchResult& Match : DiscoveryResults.Matches)
		{
			WriteSearchResultJson(Writer, Match);
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
		Writer->Close();
		return Output;
	}
}

FString FVergilReflectionParameterInfo::ToDisplayString() const
{
	return FString::Printf(
		TEXT("%s name=%s type=%s object=%s const=%s reference=%s"),
		LexReflectionParameterDirection(Direction),
		Name.IsEmpty() ? TEXT("<none>") : *Name,
		Type.IsEmpty() ? TEXT("<none>") : *Type,
		TypeObjectPath.IsEmpty() ? TEXT("<none>") : *TypeObjectPath,
		bIsConst ? TEXT("true") : TEXT("false"),
		bIsReference ? TEXT("true") : TEXT("false"));
}

FString FVergilReflectionFunctionInfo::ToDisplayString() const
{
	TArray<FString> ParameterStrings;
	ParameterStrings.Reserve(Parameters.Num());
	for (const FVergilReflectionParameterInfo& Parameter : Parameters)
	{
		ParameterStrings.Add(Parameter.ToDisplayString());
	}

	return FString::Printf(
		TEXT("function=%s owner=%s callable=%s pure=%s const=%s static=%s latent=%s params=[%s]"),
		Name.IsEmpty() ? TEXT("<none>") : *Name,
		OwnerPath.IsEmpty() ? TEXT("<none>") : *OwnerPath,
		bBlueprintCallable ? TEXT("true") : TEXT("false"),
		bBlueprintPure ? TEXT("true") : TEXT("false"),
		bConst ? TEXT("true") : TEXT("false"),
		bStatic ? TEXT("true") : TEXT("false"),
		bLatent ? TEXT("true") : TEXT("false"),
		*FString::Join(ParameterStrings, TEXT("; ")));
}

FString FVergilReflectionPropertyInfo::ToDisplayString() const
{
	return FString::Printf(
		TEXT("property=%s owner=%s type=%s object=%s blueprintVisible=%s blueprintReadOnly=%s editable=%s exposeOnSpawn=%s"),
		Name.IsEmpty() ? TEXT("<none>") : *Name,
		OwnerPath.IsEmpty() ? TEXT("<none>") : *OwnerPath,
		Type.IsEmpty() ? TEXT("<none>") : *Type,
		TypeObjectPath.IsEmpty() ? TEXT("<none>") : *TypeObjectPath,
		bBlueprintVisible ? TEXT("true") : TEXT("false"),
		bBlueprintReadOnly ? TEXT("true") : TEXT("false"),
		bEditable ? TEXT("true") : TEXT("false"),
		bExposeOnSpawn ? TEXT("true") : TEXT("false"));
}

FString FVergilReflectionEnumEntryInfo::ToDisplayString() const
{
	return FString::Printf(TEXT("entry=%s value=%lld"), Name.IsEmpty() ? TEXT("<none>") : *Name, Value);
}

FString FVergilReflectionSymbolInfo::ToDisplayString() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(
		TEXT("%s version=%d query=\"%s\" resolved=%s kind=%s name=%s path=%s super=%s"),
		ReflectionSymbolFormatName,
		ReflectionSymbolFormatVersion,
		*EscapeDisplayValue(Query),
		bResolved ? TEXT("true") : TEXT("false"),
		LexReflectionSymbolKind(Kind),
		Name.IsEmpty() ? TEXT("<none>") : *Name,
		ResolvedPath.IsEmpty() ? TEXT("<none>") : *ResolvedPath,
		SuperPath.IsEmpty() ? TEXT("<none>") : *SuperPath));

	if (!FailureReason.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("failure=\"%s\""), *EscapeDisplayValue(FailureReason)));
	}
	if (CandidatePaths.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("candidates(%d):"), CandidatePaths.Num()));
		for (const FString& CandidatePath : CandidatePaths)
		{
			Lines.Add(FString::Printf(TEXT("  - %s"), *CandidatePath));
		}
	}
	if (Functions.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("functions(%d):"), Functions.Num()));
		for (const FVergilReflectionFunctionInfo& FunctionInfo : Functions)
		{
			Lines.Add(FString::Printf(TEXT("  - %s"), *FunctionInfo.ToDisplayString()));
		}
	}
	if (Properties.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("properties(%d):"), Properties.Num()));
		for (const FVergilReflectionPropertyInfo& PropertyInfo : Properties)
		{
			Lines.Add(FString::Printf(TEXT("  - %s"), *PropertyInfo.ToDisplayString()));
		}
	}
	if (EnumEntries.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("enumEntries(%d):"), EnumEntries.Num()));
		for (const FVergilReflectionEnumEntryInfo& EntryInfo : EnumEntries)
		{
			Lines.Add(FString::Printf(TEXT("  - %s"), *EntryInfo.ToDisplayString()));
		}
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString FVergilReflectionSearchResult::ToDisplayString() const
{
	return FString::Printf(
		TEXT("kind=%s name=%s path=%s"),
		LexReflectionSymbolKind(Kind),
		Name.IsEmpty() ? TEXT("<none>") : *Name,
		ResolvedPath.IsEmpty() ? TEXT("<none>") : *ResolvedPath);
}

FString FVergilReflectionDiscoveryResults::ToDisplayString() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(
		TEXT("%s version=%d query=\"%s\" matches=%d"),
		ReflectionDiscoveryFormatName,
		ReflectionDiscoveryFormatVersion,
		*EscapeDisplayValue(Query),
		Matches.Num()));
	for (const FVergilReflectionSearchResult& Match : Matches)
	{
		Lines.Add(FString::Printf(TEXT("  - %s"), *Match.ToDisplayString()));
	}
	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::GetReflectionSymbolFormatName()
{
	return ReflectionSymbolFormatName;
}

int32 Vergil::GetReflectionSymbolFormatVersion()
{
	return ReflectionSymbolFormatVersion;
}

FString Vergil::DescribeReflectionSymbol(const FVergilReflectionSymbolInfo& SymbolInfo)
{
	return SymbolInfo.ToDisplayString();
}

FString Vergil::SerializeReflectionSymbol(const FVergilReflectionSymbolInfo& SymbolInfo, const bool bPrettyPrint)
{
	return bPrettyPrint
		? SerializeReflectionSymbolInternal<TPrettyJsonPrintPolicy<TCHAR>>(SymbolInfo)
		: SerializeReflectionSymbolInternal<TCondensedJsonPrintPolicy<TCHAR>>(SymbolInfo);
}

FVergilReflectionSymbolInfo Vergil::InspectReflectionSymbol(const FString& Query)
{
	FVergilReflectionSymbolInfo SymbolInfo;
	SymbolInfo.Query = TrimReflectionQuery(Query);
	if (SymbolInfo.Query.IsEmpty())
	{
		SymbolInfo.FailureReason = TEXT("Reflection query is empty.");
		return SymbolInfo;
	}

	if (UClass* const ResolvedClass = ResolveClassByPath(SymbolInfo.Query))
	{
		PopulateClassSymbolInfo(*ResolvedClass, SymbolInfo);
		return SymbolInfo;
	}
	if (UEnum* const ResolvedEnum = ResolveEnumByPath(SymbolInfo.Query))
	{
		PopulateEnumSymbolInfo(*ResolvedEnum, SymbolInfo);
		return SymbolInfo;
	}
	if (UScriptStruct* const ResolvedStruct = ResolveStructByPath(SymbolInfo.Query))
	{
		PopulateStructSymbolInfo(*ResolvedStruct, SymbolInfo);
		return SymbolInfo;
	}

	SymbolInfo.CandidatePaths = GatherExactShortNameCandidates(SymbolInfo.Query);
	if (SymbolInfo.CandidatePaths.Num() == 1)
	{
		return InspectReflectionSymbol(SymbolInfo.CandidatePaths[0]);
	}
	if (SymbolInfo.CandidatePaths.Num() > 1)
	{
		SymbolInfo.FailureReason = FString::Printf(
			TEXT("Reflection query '%s' is ambiguous; inspect one of the candidate paths instead."),
			*SymbolInfo.Query);
		return SymbolInfo;
	}

	SymbolInfo.FailureReason = FString::Printf(TEXT("Unable to resolve reflection query '%s'."), *SymbolInfo.Query);
	return SymbolInfo;
}

FString Vergil::GetReflectionDiscoveryFormatName()
{
	return ReflectionDiscoveryFormatName;
}

int32 Vergil::GetReflectionDiscoveryFormatVersion()
{
	return ReflectionDiscoveryFormatVersion;
}

FString Vergil::DescribeReflectionDiscovery(const FVergilReflectionDiscoveryResults& DiscoveryResults)
{
	return DiscoveryResults.ToDisplayString();
}

FString Vergil::SerializeReflectionDiscovery(const FVergilReflectionDiscoveryResults& DiscoveryResults, const bool bPrettyPrint)
{
	return bPrettyPrint
		? SerializeReflectionDiscoveryInternal<TPrettyJsonPrintPolicy<TCHAR>>(DiscoveryResults)
		: SerializeReflectionDiscoveryInternal<TCondensedJsonPrintPolicy<TCHAR>>(DiscoveryResults);
}

FVergilReflectionDiscoveryResults Vergil::DiscoverReflectionSymbols(const FString& Query, const int32 MaxResults)
{
	FVergilReflectionDiscoveryResults DiscoveryResults;
	DiscoveryResults.Query = TrimReflectionQuery(Query);
	if (DiscoveryResults.Query.IsEmpty())
	{
		return DiscoveryResults;
	}

	struct FSortableSearchResult
	{
		FVergilReflectionSearchResult Match;
		bool bExactName = false;
		bool bPrefixName = false;
	};

	TArray<FSortableSearchResult> Matches;
	TSet<FString> SeenPaths;
	auto AddMatch = [&DiscoveryResults, &Matches, &SeenPaths](const UObject* const Object, const EVergilReflectionSymbolKind Kind)
	{
		if (!IsDiscoverableObject(Object))
		{
			return;
		}

		const FString Name = Object->GetName();
		const FString Path = Object->GetPathName();
		if (!Name.Contains(DiscoveryResults.Query, ESearchCase::IgnoreCase)
			&& !Path.Contains(DiscoveryResults.Query, ESearchCase::IgnoreCase))
		{
			return;
		}
		if (SeenPaths.Contains(Path))
		{
			return;
		}

		SeenPaths.Add(Path);
		FSortableSearchResult SearchResult;
		SearchResult.Match.Kind = Kind;
		SearchResult.Match.Name = Name;
		SearchResult.Match.ResolvedPath = Path;
		SearchResult.bExactName = Name.Equals(DiscoveryResults.Query, ESearchCase::IgnoreCase);
		SearchResult.bPrefixName = Name.StartsWith(DiscoveryResults.Query, ESearchCase::IgnoreCase);
		Matches.Add(MoveTemp(SearchResult));
	};

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (IsDiscoverableClass(*It))
		{
			AddMatch(*It, EVergilReflectionSymbolKind::Class);
		}
	}
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (IsDiscoverableObject(*It))
		{
			AddMatch(*It, EVergilReflectionSymbolKind::Struct);
		}
	}
	for (TObjectIterator<UEnum> It; It; ++It)
	{
		if (IsDiscoverableObject(*It))
		{
			AddMatch(*It, EVergilReflectionSymbolKind::Enum);
		}
	}

	Matches.Sort([](const FSortableSearchResult& Left, const FSortableSearchResult& Right)
	{
		if (Left.bExactName != Right.bExactName)
		{
			return Left.bExactName && !Right.bExactName;
		}
		if (Left.bPrefixName != Right.bPrefixName)
		{
			return Left.bPrefixName && !Right.bPrefixName;
		}
		return Left.Match.Name == Right.Match.Name
			? Left.Match.ResolvedPath < Right.Match.ResolvedPath
			: Left.Match.Name < Right.Match.Name;
	});

	const int32 ResultLimit = MaxResults < 0 ? Matches.Num() : FMath::Min(MaxResults, Matches.Num());
	DiscoveryResults.Matches.Reserve(ResultLimit);
	for (int32 MatchIndex = 0; MatchIndex < ResultLimit; ++MatchIndex)
	{
		DiscoveryResults.Matches.Add(Matches[MatchIndex].Match);
	}

	return DiscoveryResults;
}
