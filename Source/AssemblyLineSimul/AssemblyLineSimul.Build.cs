// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class AssemblyLineSimul : ModuleRules
{
	public AssemblyLineSimul(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore",
			"AIModule", "NavigationSystem",
			"GameplayTags", "GameplayTasks",
			"UMG", "Slate", "SlateCore",
			"EnhancedInput"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"HTTP", "Json", "JsonUtilities",
			"FunctionalTesting"
		});

		PrivateIncludePaths.Add(ModuleDirectory);

		// Voice capture on macOS uses AVAudioRecorder (Obj-C++ in MacAudioCapture.mm).
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.AddRange(new string[] { "AVFoundation", "Foundation", "CoreAudio" });
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
