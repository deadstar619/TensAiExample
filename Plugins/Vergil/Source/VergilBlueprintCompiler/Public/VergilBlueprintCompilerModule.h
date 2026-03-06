#pragma once

#include "Modules/ModuleManager.h"

class FVergilBlueprintCompilerModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
