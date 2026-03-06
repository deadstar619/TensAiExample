#pragma once

#include "Modules/ModuleManager.h"

class FVergilAgentModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
