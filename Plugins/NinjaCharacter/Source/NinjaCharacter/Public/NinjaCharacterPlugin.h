// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"


/**
 * The public interface to this plugin module.
 */
class INinjaCharacterPlugin : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to this module's interface.
	 * @return singleton instance, loading the module on demand if needed
	 */
	static inline INinjaCharacterPlugin& Get()
	{
		return FModuleManager::LoadModuleChecked<INinjaCharacterPlugin>(TEXT("NinjaCharacter"));
	}

	/**
	 * Checks to see if this module is loaded and ready.
	 * @return true if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("NinjaCharacter"));
	}
};
