// "Ninja Character" plugin, by Javier 'Xaklse' Osset; Copyright 2020


namespace UnrealBuildTool.Rules
{
	public class NinjaCharacter : ModuleRules
	{
		public NinjaCharacter(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("NinjaCharacter/Private");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AIModule",
					"Chaos",
					"ChaosCore",
					"HeadMountedDisplay",
					"PerfCounters",
					"PhysicsCore",
				}
			);

			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		}
	}
}
