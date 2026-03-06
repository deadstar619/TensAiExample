#pragma once

#include "CoreMinimal.h"
#include "VergilCompilerPass.h"

class VERGILBLUEPRINTCOMPILER_API FVergilSchemaMigrationPass final : public IVergilCompilerPass
{
public:
	virtual FName GetPassName() const override;
	virtual bool Run(const FVergilCompileRequest& Request, FVergilCompilerContext& Context, FVergilCompileResult& Result) const override;
};

class VERGILBLUEPRINTCOMPILER_API FVergilSemanticValidationPass final : public IVergilCompilerPass
{
public:
	virtual FName GetPassName() const override;
	virtual bool Run(const FVergilCompileRequest& Request, FVergilCompilerContext& Context, FVergilCompileResult& Result) const override;
};

class VERGILBLUEPRINTCOMPILER_API FVergilSymbolResolutionPass final : public IVergilCompilerPass
{
public:
	virtual FName GetPassName() const override;
	virtual bool Run(const FVergilCompileRequest& Request, FVergilCompilerContext& Context, FVergilCompileResult& Result) const override;
};

class VERGILBLUEPRINTCOMPILER_API FVergilStructuralValidationPass final : public IVergilCompilerPass
{
public:
	virtual FName GetPassName() const override;
	virtual bool Run(const FVergilCompileRequest& Request, FVergilCompilerContext& Context, FVergilCompileResult& Result) const override;
};

class VERGILBLUEPRINTCOMPILER_API FVergilCommandPlanningPass final : public IVergilCompilerPass
{
public:
	virtual FName GetPassName() const override;
	virtual bool Run(const FVergilCompileRequest& Request, FVergilCompilerContext& Context, FVergilCompileResult& Result) const override;
};
