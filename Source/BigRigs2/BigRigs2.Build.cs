// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BigRigs2 : ModuleRules
{
	public BigRigs2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "ChaosVehicles", "PhysicsCore", "NinjaCharacter"});
	}
}
