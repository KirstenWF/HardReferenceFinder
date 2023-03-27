// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HardReferenceViewer : ModuleRules
{
	public HardReferenceViewer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"InputCore",
				"UnrealEd",
				"ToolMenus",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"Kismet",
				"AssetRegistry",
				"BlueprintGraph",
				"AssetTools",
			}
			);

		if (Target.Version.MajorVersion < 5)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"EditorStyle", // only used in <UE4
				});
		}
		else // MajorVersion >= 5
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"SubobjectEditor",
				});
		}
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
