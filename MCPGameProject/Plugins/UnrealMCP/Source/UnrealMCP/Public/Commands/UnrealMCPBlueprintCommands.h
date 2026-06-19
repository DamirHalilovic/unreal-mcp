#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Blueprint-related MCP commands
 */
class UNREALMCP_API FUnrealMCPBlueprintCommands
{
public:
    FUnrealMCPBlueprintCommands();

    // Handle blueprint commands
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    // Specific blueprint command handlers
    TSharedPtr<FJsonObject> HandleCreateBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddComponentToBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetComponentProperty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPhysicsProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetBlueprintProperty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetStaticMeshProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPawnProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBlueprintInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBlueprintGraph(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDiffBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDiffAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAssetInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBehaviorTree(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBlackboard(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAnalyzeTrace(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCaptureMemreport(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleStartTrace(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleStopTrace(const TSharedPtr<FJsonObject>& Params);

    // Helper functions
    TSharedPtr<FJsonObject> AddComponentToBlueprint(const FString& BlueprintName, const FString& ComponentType, 
                                                   const FString& ComponentName, const FString& MeshType,
                                                   const TArray<float>& Location, const TArray<float>& Rotation,
                                                   const TArray<float>& Scale, const TSharedPtr<FJsonObject>& ComponentProperties);
}; 