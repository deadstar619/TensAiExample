#include "VergilDeveloperSettings.h"

UVergilDeveloperSettings::UVergilDeveloperSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Vergil");
}

FName UVergilDeveloperSettings::GetContainerName() const
{
	return TEXT("Project");
}

FName UVergilDeveloperSettings::GetCategoryName() const
{
	return CategoryName;
}

FName UVergilDeveloperSettings::GetSectionName() const
{
	return SectionName;
}
