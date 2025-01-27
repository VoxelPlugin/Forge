// Copyright Voxel Plugin SAS. All Rights Reserved.

using UnrealBuildTool;

public class Forge : ModuleRules
{
	public Forge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"Json",
			"HTTP",
			"Projects",
		});
	}
}
