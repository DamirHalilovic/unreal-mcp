#include "Commands/UnrealMCPBlueprintCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MacroInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UnrealType.h"
#include "Misc/Paths.h"
#include "GraphDiffControl.h"
#include "DiffResults.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Misc/PackageName.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "DiffUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Model/Counters.h"
#include "TraceServices/Model/Memory.h"
#include "TraceServices/Model/Regions.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/LoadTimeProfiler.h"
#include "TraceServices/Containers/Tables.h"
#include "TraceServices/ModuleService.h"
#include "Common/ProviderLock.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Engine/Engine.h"

FUnrealMCPBlueprintCommands::FUnrealMCPBlueprintCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_blueprint"))
    {
        return HandleCreateBlueprint(Params);
    }
    else if (CommandType == TEXT("add_component_to_blueprint"))
    {
        return HandleAddComponentToBlueprint(Params);
    }
    else if (CommandType == TEXT("set_component_property"))
    {
        return HandleSetComponentProperty(Params);
    }
    else if (CommandType == TEXT("set_physics_properties"))
    {
        return HandleSetPhysicsProperties(Params);
    }
    else if (CommandType == TEXT("compile_blueprint"))
    {
        return HandleCompileBlueprint(Params);
    }
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    else if (CommandType == TEXT("set_blueprint_property"))
    {
        return HandleSetBlueprintProperty(Params);
    }
    else if (CommandType == TEXT("set_static_mesh_properties"))
    {
        return HandleSetStaticMeshProperties(Params);
    }
    else if (CommandType == TEXT("set_pawn_properties"))
    {
        return HandleSetPawnProperties(Params);
    }
    else if (CommandType == TEXT("get_blueprint_info"))
    {
        return HandleGetBlueprintInfo(Params);
    }
    else if (CommandType == TEXT("get_blueprint_graph"))
    {
        return HandleGetBlueprintGraph(Params);
    }
    else if (CommandType == TEXT("diff_blueprint"))
    {
        return HandleDiffBlueprint(Params);
    }
    else if (CommandType == TEXT("diff_asset"))
    {
        return HandleDiffAsset(Params);
    }
    else if (CommandType == TEXT("get_asset_info"))
    {
        return HandleGetAssetInfo(Params);
    }
    else if (CommandType == TEXT("analyze_trace"))
    {
        return HandleAnalyzeTrace(Params);
    }
    else if (CommandType == TEXT("capture_memreport"))
    {
        return HandleCaptureMemreport(Params);
    }
    else if (CommandType == TEXT("start_trace"))
    {
        return HandleStartTrace(Params);
    }
    else if (CommandType == TEXT("stop_trace"))
    {
        return HandleStopTrace(Params);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint command: %s"), *CommandType));
}

namespace
{
    FString MCPPinTypeToString(const FEdGraphPinType& PinType)
    {
        FString Base = PinType.PinCategory.ToString();
        if (PinType.PinSubCategoryObject.IsValid())
        {
            Base += FString::Printf(TEXT("(%s)"), *PinType.PinSubCategoryObject->GetName());
        }
        else if (!PinType.PinSubCategory.IsNone())
        {
            Base += FString::Printf(TEXT("(%s)"), *PinType.PinSubCategory.ToString());
        }
        switch (PinType.ContainerType)
        {
            case EPinContainerType::Array: Base += TEXT("[]"); break;
            case EPinContainerType::Set:   Base += TEXT("{set}"); break;
            case EPinContainerType::Map:   Base += TEXT("{map}"); break;
            default: break;
        }
        return Base;
    }

    // Resolve a blueprint by bare name (anywhere in the project), full object path,
    // or the legacy /Game/Blueprints/<name> location.
    UBlueprint* MCPFindBlueprintFlexible(const FString& Identifier)
    {
        if (Identifier.IsEmpty())
        {
            return nullptr;
        }
        if (Identifier.StartsWith(TEXT("/")))
        {
            if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Identifier))
            {
                return BP;
            }
        }
        // Legacy /Game/Blueprints/<name> location — quiet, since most assets live
        // elsewhere and the asset-registry search below is the real resolver.
        const FString LegacyPath = TEXT("/Game/Blueprints/") + Identifier;
        if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *LegacyPath, nullptr, LOAD_NoWarn))
        {
            return BP;
        }

        FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        FARFilter Filter;
        Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
        Filter.bRecursiveClasses = true;
        TArray<FAssetData> Assets;
        ARM.Get().GetAssets(Filter, Assets);

        const FString Wanted = FPaths::GetBaseFilename(Identifier);
        for (const FAssetData& Data : Assets)
        {
            if (Data.AssetName.ToString() == Wanted)
            {
                return Cast<UBlueprint>(Data.GetAsset());
            }
        }
        return nullptr;
    }

    TArray<TSharedPtr<FJsonValue>> MCPGraphsToJson(const TArray<UEdGraph*>& Graphs)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (UEdGraph* Graph : Graphs)
        {
            if (!Graph)
            {
                continue;
            }
            TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
            GraphObj->SetStringField(TEXT("name"), Graph->GetName());
            GraphObj->SetNumberField(TEXT("num_nodes"), Graph->Nodes.Num());
            Out.Add(MakeShared<FJsonValueObject>(GraphObj));
        }
        return Out;
    }

    struct FMCPGraphEntry { FString Category; UEdGraph* Graph; };

    void MCPAddGraphRecursive(UEdGraph* Graph, const FString& Category, TArray<FMCPGraphEntry>& Out)
    {
        if (!Graph)
        {
            return;
        }
        Out.Add({ Category, Graph });
        for (UEdGraph* Sub : Graph->SubGraphs)
        {
            MCPAddGraphRecursive(Sub, TEXT("subgraph"), Out);
        }
    }

    // Every graph in the blueprint: event graphs, functions, the construction
    // script, macros, delegate signatures, and any nested (collapsed) subgraphs.
    TArray<FMCPGraphEntry> MCPCollectAllGraphs(UBlueprint* Blueprint)
    {
        TArray<FMCPGraphEntry> Out;
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            MCPAddGraphRecursive(Graph, TEXT("event"), Out);
        }
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            const bool bCtor = Graph && Graph->GetName() == TEXT("UserConstructionScript");
            MCPAddGraphRecursive(Graph, bCtor ? TEXT("construction") : TEXT("function"), Out);
        }
        for (UEdGraph* Graph : Blueprint->MacroGraphs)
        {
            MCPAddGraphRecursive(Graph, TEXT("macro"), Out);
        }
        for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
        {
            MCPAddGraphRecursive(Graph, TEXT("delegate"), Out);
        }
        return Out;
    }

    FString MCPDiffCategoryToString(EDiffType::Category Category)
    {
        switch (Category)
        {
            case EDiffType::ADDITION:     return TEXT("addition");
            case EDiffType::SUBTRACTION:  return TEXT("subtraction");
            case EDiffType::MODIFICATION: return TEXT("modification");
            case EDiffType::MINOR:        return TEXT("minor");
            case EDiffType::CONTROL:      return TEXT("control");
            default:                      return TEXT("unknown");
        }
    }

    // Load a .uasset from an arbitrary path as a separate copy for diffing against
    // the live asset, mirroring the engine's own -diff load path (copy into DiffDir,
    // then LoadPackage with LOAD_ForDiff so it doesn't clobber the loaded original).
    UBlueprint* MCPLoadBlueprintForDiff(const FString& InPath)
    {
        if (!FPaths::FileExists(InPath))
        {
            return nullptr;
        }
        FString BaseName = FPaths::GetBaseFilename(InPath);
        const TCHAR* Invalid = INVALID_LONGPACKAGE_CHARACTERS;
        for (; *Invalid; ++Invalid)
        {
            const TCHAR InvalidStr[] = { *Invalid, '\0' };
            BaseName.ReplaceInline(InvalidStr, TEXT("_"));
        }
        // Unique per load: re-diffing the same file must not collide with a package
        // already loaded for diff this session.
        const FString DiffPath = FString::Printf(TEXT("%s%s_mcpdiff_%s%s"), *FPaths::DiffDir(), *BaseName, *FGuid::NewGuid().ToString(EGuidFormats::Digits), *FPaths::GetExtension(InPath, true));
        if (IFileManager::Get().Copy(*DiffPath, *InPath, true, true) != COPY_OK)
        {
            return nullptr;
        }
        UPackage* Package = LoadPackage(nullptr, *DiffPath, LOAD_ForDiff);
        if (!Package)
        {
            return nullptr;
        }
        TArray<UObject*> Objects;
        GetObjectsWithPackage(Package, Objects);
        for (UObject* Obj : Objects)
        {
            if (UBlueprint* BP = Cast<UBlueprint>(Obj))
            {
                return BP;
            }
        }
        return nullptr;
    }

    // Resolve ANY asset (not just blueprints) by path or bare name.
    UObject* MCPFindAssetFlexible(const FString& Identifier)
    {
        if (Identifier.IsEmpty())
        {
            return nullptr;
        }
        if (Identifier.StartsWith(TEXT("/")))
        {
            if (UObject* Obj = LoadObject<UObject>(nullptr, *Identifier))
            {
                return Obj;
            }
        }
        FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        TArray<FAssetData> Assets;
        ARM.Get().GetAllAssets(Assets);
        const FString Wanted = FPaths::GetBaseFilename(Identifier);
        for (const FAssetData& Data : Assets)
        {
            if (Data.AssetName.ToString() == Wanted)
            {
                return Data.GetAsset();
            }
        }
        return nullptr;
    }

    // Load ANY asset's older version for diff (the RF_Standalone primary object).
    UObject* MCPLoadAssetForDiff(const FString& InPath)
    {
        if (!FPaths::FileExists(InPath))
        {
            return nullptr;
        }
        FString BaseName = FPaths::GetBaseFilename(InPath);
        const TCHAR* Invalid = INVALID_LONGPACKAGE_CHARACTERS;
        for (; *Invalid; ++Invalid)
        {
            const TCHAR InvalidStr[] = { *Invalid, '\0' };
            BaseName.ReplaceInline(InvalidStr, TEXT("_"));
        }
        const FString DiffPath = FString::Printf(TEXT("%s%s_mcpdiff_%s%s"), *FPaths::DiffDir(), *BaseName, *FGuid::NewGuid().ToString(EGuidFormats::Digits), *FPaths::GetExtension(InPath, true));
        if (IFileManager::Get().Copy(*DiffPath, *InPath, true, true) != COPY_OK)
        {
            return nullptr;
        }
        UPackage* Package = LoadPackage(nullptr, *DiffPath, LOAD_ForDiff);
        if (!Package)
        {
            return nullptr;
        }
        TArray<UObject*> Objects;
        GetObjectsWithPackage(Package, Objects);
        UObject* Standalone = nullptr;   // normal asset (texture/blueprint/data asset)
        UObject* Actor = nullptr;        // external-actor package: the actor IS the asset
        UObject* Fallback = nullptr;
        for (UObject* Obj : Objects)
        {
            if (!Obj || Obj->IsA<UPackage>() || Obj->HasAnyFlags(RF_ClassDefaultObject))
            {
                continue;
            }
            if (Obj->GetClass()->GetName() == TEXT("MetaData"))
            {
                continue;
            }
            if (!Actor && Obj->IsA<AActor>())
            {
                Actor = Obj;  // OFPA actors aren't outered to the package, so don't filter on that
            }
            if (Obj->GetOuter() == Package)
            {
                if (!Standalone && Obj->HasAnyFlags(RF_Standalone))
                {
                    Standalone = Obj;
                }
                if (!Fallback)
                {
                    Fallback = Obj;
                }
            }
        }
        // A normal asset is the standalone top-level object; an external-actor package
        // has no standalone asset, so use the actor.
        if (Standalone) { return Standalone; }
        if (Actor) { return Actor; }
        return Fallback;
    }

    FString MCPPropertyDiffTypeToString(EPropertyDiffType::Type DiffType)
    {
        switch (DiffType)
        {
            case EPropertyDiffType::PropertyAddedToA:     return TEXT("removed");
            case EPropertyDiffType::PropertyAddedToB:     return TEXT("added");
            case EPropertyDiffType::PropertyValueChanged: return TEXT("changed");
            default:                                      return TEXT("unknown");
        }
    }

    // Resolve a property path on an object and export its value as text. FResolvedProperty.Object
    // is the leaf's CONTAINER (DiffUtils.cpp Resolve), so the value is ContainerPtrToValuePtr.
    FString MCPExportResolvedValue(const FPropertySoftPath& Path, const UObject* Obj)
    {
        if (!Obj)
        {
            return FString();
        }
        FResolvedProperty Resolved = Path.Resolve(Obj);
        if (!Resolved.Property || !Resolved.Object)
        {
            return FString();
        }
        FString Value;
        Resolved.Property->ExportTextItem_Direct(Value, Resolved.Property->ContainerPtrToValuePtr<void>(Resolved.Object), nullptr, nullptr, PPF_None);
        if (Value.Len() > 240)
        {
            Value = Value.Left(240) + TEXT("…");
        }
        return Value;
    }

    // True if the property is an object reference to one of the actor's components.
    // Across two loaded versions such refs differ only by package path (pure noise);
    // real component changes are reported by the per-component diff instead.
    bool MCPIsComponentRef(const FPropertySoftPath& Path, const UObject* Obj)
    {
        if (!Obj)
        {
            return false;
        }
        FResolvedProperty Resolved = Path.Resolve(Obj);
        const FObjectPropertyBase* ObjProp = Resolved.Property ? CastField<FObjectPropertyBase>(Resolved.Property) : nullptr;
        if (!ObjProp || !Resolved.Object)
        {
            return false;
        }
        const UObject* Ref = ObjProp->GetObjectPropertyValue(Resolved.Property->ContainerPtrToValuePtr<void>(Resolved.Object));
        return Ref && Ref->IsA<UActorComponent>();
    }

    // Pair graphs by name between two blueprints and append the FGraphDiffControl
    // results as JSON. Shared by diff_blueprint and diff_asset.
    void MCPAppendBlueprintGraphDiffs(UBlueprint* BaseBP, UBlueprint* CurrentBP, const FString& GraphFilter,
                                      TArray<TSharedPtr<FJsonValue>>& Diffs, int32& GraphsCompared)
    {
        TMap<FString, UEdGraph*> BaseByName;
        for (const FMCPGraphEntry& Entry : MCPCollectAllGraphs(BaseBP))
        {
            if (Entry.Graph) { BaseByName.Add(Entry.Graph->GetName(), Entry.Graph); }
        }
        TMap<FString, UEdGraph*> CurrentByName;
        for (const FMCPGraphEntry& Entry : MCPCollectAllGraphs(CurrentBP))
        {
            if (Entry.Graph) { CurrentByName.Add(Entry.Graph->GetName(), Entry.Graph); }
        }

        auto EmitPresence = [&Diffs](const FString& GraphName, const TCHAR* Category, const FString& Display)
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("graph"), GraphName);
            Entry->SetStringField(TEXT("category"), Category);
            Entry->SetStringField(TEXT("display"), Display);
            Diffs.Add(MakeShared<FJsonValueObject>(Entry));
        };

        for (const TPair<FString, UEdGraph*>& Pair : CurrentByName)
        {
            const FString& Name = Pair.Key;
            if (!GraphFilter.IsEmpty() && !Name.Equals(GraphFilter, ESearchCase::IgnoreCase)) { continue; }
            UEdGraph** BaseGraph = BaseByName.Find(Name);
            if (!BaseGraph)
            {
                EmitPresence(Name, TEXT("addition"), FString::Printf(TEXT("Graph '%s' added"), *Name));
                continue;
            }
            ++GraphsCompared;
            TArray<FDiffSingleResult> Results;
            FGraphDiffControl::DiffGraphs(*BaseGraph, Pair.Value, Results);
            for (const FDiffSingleResult& Result : Results)
            {
                if (Result.Diff == EDiffType::NO_DIFFERENCE) { continue; }
                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("graph"), Name);
                Entry->SetStringField(TEXT("category"), MCPDiffCategoryToString(Result.Category));
                Entry->SetStringField(TEXT("display"), Result.DisplayString.ToString());
                if (Result.Node1) { Entry->SetStringField(TEXT("base_node"), Result.Node1->NodeGuid.ToString()); }
                if (Result.Node2) { Entry->SetStringField(TEXT("current_node"), Result.Node2->NodeGuid.ToString()); }
                if (Result.Pin1) { Entry->SetStringField(TEXT("base_pin"), Result.Pin1->PinName.ToString()); }
                if (Result.Pin2) { Entry->SetStringField(TEXT("current_pin"), Result.Pin2->PinName.ToString()); }
                Diffs.Add(MakeShared<FJsonValueObject>(Entry));
            }
        }

        for (const TPair<FString, UEdGraph*>& Pair : BaseByName)
        {
            const FString& Name = Pair.Key;
            if (!GraphFilter.IsEmpty() && !Name.Equals(GraphFilter, ESearchCase::IgnoreCase)) { continue; }
            if (!CurrentByName.Contains(Name))
            {
                EmitPresence(Name, TEXT("subtraction"), FString::Printf(TEXT("Graph '%s' removed"), *Name));
            }
        }
    }

    // Actor-aware diff: an actor's meaningful changes (transform, mesh, materials) live
    // inside components, which CompareUnrelatedObjects refuses to recurse into. So pair
    // components by name and diff each directly, and report components added/removed.
    void MCPAppendActorComponentDiffs(AActor* BaseActor, AActor* CurActor, const FString& ActorLabel, TArray<TSharedPtr<FJsonValue>>& Diffs)
    {
        auto Tag = [&ActorLabel](TSharedPtr<FJsonObject> Entry) -> TSharedPtr<FJsonObject>
        {
            if (!ActorLabel.IsEmpty()) { Entry->SetStringField(TEXT("actor"), ActorLabel); }
            return Entry;
        };

        TArray<UActorComponent*> BaseComps;
        TArray<UActorComponent*> CurComps;
        BaseActor->GetComponents(BaseComps);
        CurActor->GetComponents(CurComps);
        TMap<FString, UActorComponent*> BaseByName;
        TMap<FString, UActorComponent*> CurByName;
        for (UActorComponent* Comp : BaseComps) { if (Comp) { BaseByName.Add(Comp->GetName(), Comp); } }
        for (UActorComponent* Comp : CurComps) { if (Comp) { CurByName.Add(Comp->GetName(), Comp); } }

        for (const TPair<FString, UActorComponent*>& Pair : CurByName)
        {
            const FString& Name = Pair.Key;
            UActorComponent** BaseComp = BaseByName.Find(Name);
            if (!BaseComp)
            {
                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("component"), Name);
                Entry->SetStringField(TEXT("change"), TEXT("component_added"));
                Entry->SetStringField(TEXT("current_value"), Pair.Value->GetClass()->GetName());
                Diffs.Add(MakeShared<FJsonValueObject>(Tag(Entry)));
                continue;
            }
            TArray<FSingleObjectDiffEntry> Entries;
            DiffUtils::CompareUnrelatedObjects(*BaseComp, Pair.Value, Entries);
            for (const FSingleObjectDiffEntry& Entry : Entries)
            {
                TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
                Obj->SetStringField(TEXT("component"), Name);
                Obj->SetStringField(TEXT("property"), Entry.Identifier.ToDisplayName());
                Obj->SetStringField(TEXT("change"), MCPPropertyDiffTypeToString(Entry.DiffType));
                if (Entry.DiffType != EPropertyDiffType::PropertyAddedToB)
                {
                    Obj->SetStringField(TEXT("base_value"), MCPExportResolvedValue(Entry.Identifier, *BaseComp));
                }
                if (Entry.DiffType != EPropertyDiffType::PropertyAddedToA)
                {
                    Obj->SetStringField(TEXT("current_value"), MCPExportResolvedValue(Entry.Identifier, Pair.Value));
                }
                Diffs.Add(MakeShared<FJsonValueObject>(Tag(Obj)));
            }
        }
        for (const TPair<FString, UActorComponent*>& Pair : BaseByName)
        {
            if (!CurByName.Contains(Pair.Key))
            {
                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("component"), Pair.Key);
                Entry->SetStringField(TEXT("change"), TEXT("component_removed"));
                Entry->SetStringField(TEXT("base_value"), Pair.Value->GetClass()->GetName());
                Diffs.Add(MakeShared<FJsonValueObject>(Tag(Entry)));
            }
        }
    }

    // Full single-actor diff: actor-level properties + per-component diffs, all tagged
    // with the actor label.
    void MCPDiffActor(AActor* BaseActor, AActor* CurActor, const FString& ActorLabel, TArray<TSharedPtr<FJsonValue>>& Diffs)
    {
        TArray<FSingleObjectDiffEntry> ActorEntries;
        DiffUtils::CompareUnrelatedObjects(BaseActor, CurActor, ActorEntries);
        for (const FSingleObjectDiffEntry& Entry : ActorEntries)
        {
            if (MCPIsComponentRef(Entry.Identifier, CurActor)) { continue; }
            TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
            if (!ActorLabel.IsEmpty()) { Obj->SetStringField(TEXT("actor"), ActorLabel); }
            Obj->SetStringField(TEXT("property"), Entry.Identifier.ToDisplayName());
            Obj->SetStringField(TEXT("change"), MCPPropertyDiffTypeToString(Entry.DiffType));
            if (Entry.DiffType != EPropertyDiffType::PropertyAddedToB)
            {
                Obj->SetStringField(TEXT("base_value"), MCPExportResolvedValue(Entry.Identifier, BaseActor));
            }
            if (Entry.DiffType != EPropertyDiffType::PropertyAddedToA)
            {
                Obj->SetStringField(TEXT("current_value"), MCPExportResolvedValue(Entry.Identifier, CurActor));
            }
            Diffs.Add(MakeShared<FJsonValueObject>(Obj));
        }
        MCPAppendActorComponentDiffs(BaseActor, CurActor, ActorLabel, Diffs);
    }

    // Diff the actors of two worlds' persistent levels (non-OFPA maps embed their
    // actors here; for World Partition maps this is mostly streaming/world setup).
    void MCPDiffLevelActors(UWorld* BaseWorld, UWorld* CurWorld, TArray<TSharedPtr<FJsonValue>>& Diffs)
    {
        TMap<FString, AActor*> BaseByName;
        TMap<FString, AActor*> CurByName;
        if (BaseWorld->PersistentLevel)
        {
            for (AActor* Actor : BaseWorld->PersistentLevel->Actors) { if (Actor) { BaseByName.Add(Actor->GetName(), Actor); } }
        }
        if (CurWorld->PersistentLevel)
        {
            for (AActor* Actor : CurWorld->PersistentLevel->Actors) { if (Actor) { CurByName.Add(Actor->GetName(), Actor); } }
        }

        for (const TPair<FString, AActor*>& Pair : CurByName)
        {
            AActor** BaseActor = BaseByName.Find(Pair.Key);
            if (!BaseActor)
            {
                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("actor"), Pair.Key);
                Entry->SetStringField(TEXT("change"), TEXT("actor_added"));
                Entry->SetStringField(TEXT("current_value"), Pair.Value->GetClass()->GetName());
                Diffs.Add(MakeShared<FJsonValueObject>(Entry));
                continue;
            }
            MCPDiffActor(*BaseActor, Pair.Value, Pair.Key, Diffs);
        }
        for (const TPair<FString, AActor*>& Pair : BaseByName)
        {
            if (!CurByName.Contains(Pair.Key))
            {
                TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("actor"), Pair.Key);
                Entry->SetStringField(TEXT("change"), TEXT("actor_removed"));
                Entry->SetStringField(TEXT("base_value"), Pair.Value->GetClass()->GetName());
                Diffs.Add(MakeShared<FJsonValueObject>(Entry));
            }
        }
    }
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetBlueprintInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    UBlueprint* Blueprint = MCPFindBlueprintFlexible(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Blueprint->GetName());
    Data->SetStringField(TEXT("path"), Blueprint->GetPathName());
    if (Blueprint->ParentClass)
    {
        Data->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());
    }

    TArray<TSharedPtr<FJsonValue>> Components;
    if (Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (!Node)
            {
                continue;
            }
            TSharedPtr<FJsonObject> Comp = MakeShared<FJsonObject>();
            Comp->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
            if (Node->ComponentTemplate)
            {
                Comp->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
            }
            if (USCS_Node* Parent = Blueprint->SimpleConstructionScript->FindParentNode(Node))
            {
                Comp->SetStringField(TEXT("attached_to"), Parent->GetVariableName().ToString());
            }
            Components.Add(MakeShared<FJsonValueObject>(Comp));
        }
    }
    Data->SetArrayField(TEXT("components"), Components);

    // Inherited native components from the parent class CDO.
    TArray<TSharedPtr<FJsonValue>> InheritedComponents;
    if (Blueprint->ParentClass)
    {
        if (AActor* ParentCDO = Cast<AActor>(Blueprint->ParentClass->GetDefaultObject()))
        {
            for (UActorComponent* Comp : ParentCDO->GetComponents())
            {
                if (!Comp)
                {
                    continue;
                }
                TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
                CompObj->SetStringField(TEXT("name"), Comp->GetName());
                CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
                InheritedComponents.Add(MakeShared<FJsonValueObject>(CompObj));
            }
        }
    }
    Data->SetArrayField(TEXT("inherited_components"), InheritedComponents);

    TArray<TSharedPtr<FJsonValue>> Variables;
    for (const FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), MCPPinTypeToString(Var.VarType));
        Variables.Add(MakeShared<FJsonValueObject>(VarObj));
    }
    Data->SetArrayField(TEXT("variables"), Variables);

    // Inherited Blueprint-visible variables from the native parent class.
    TArray<TSharedPtr<FJsonValue>> InheritedVariables;
    if (Blueprint->ParentClass)
    {
        for (TFieldIterator<FProperty> It(Blueprint->ParentClass); It; ++It)
        {
            FProperty* Prop = *It;
            if (!Prop || !Prop->HasAnyPropertyFlags(CPF_BlueprintVisible))
            {
                continue;
            }
            TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
            VarObj->SetStringField(TEXT("name"), Prop->GetName());
            VarObj->SetStringField(TEXT("type"), Prop->GetCPPType());
            VarObj->SetStringField(TEXT("from"), Prop->GetOwnerClass() ? Prop->GetOwnerClass()->GetName() : TEXT(""));
            InheritedVariables.Add(MakeShared<FJsonValueObject>(VarObj));
        }
    }
    Data->SetArrayField(TEXT("inherited_variables"), InheritedVariables);

    Data->SetArrayField(TEXT("functions"), MCPGraphsToJson(Blueprint->FunctionGraphs));

    TSharedPtr<FJsonObject> Graphs = MakeShared<FJsonObject>();
    Graphs->SetArrayField(TEXT("event_graphs"), MCPGraphsToJson(Blueprint->UbergraphPages));
    Graphs->SetArrayField(TEXT("function_graphs"), MCPGraphsToJson(Blueprint->FunctionGraphs));
    Graphs->SetArrayField(TEXT("macro_graphs"), MCPGraphsToJson(Blueprint->MacroGraphs));
    Data->SetObjectField(TEXT("graphs"), Graphs);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString GraphName;
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    UBlueprint* Blueprint = MCPFindBlueprintFlexible(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    const TArray<FMCPGraphEntry> AllGraphs = MCPCollectAllGraphs(Blueprint);

    // No graph named: list every graph (event / function / construction / macro /
    // delegate / nested subgraph) with its category so the caller can pick one.
    if (GraphName.IsEmpty())
    {
        TSharedPtr<FJsonObject> ListData = MakeShared<FJsonObject>();
        ListData->SetStringField(TEXT("blueprint"), Blueprint->GetName());
        TArray<TSharedPtr<FJsonValue>> List;
        for (const FMCPGraphEntry& Entry : AllGraphs)
        {
            TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
            GraphObj->SetStringField(TEXT("name"), Entry.Graph->GetName());
            GraphObj->SetStringField(TEXT("category"), Entry.Category);
            GraphObj->SetNumberField(TEXT("num_nodes"), Entry.Graph->Nodes.Num());
            List.Add(MakeShared<FJsonValueObject>(GraphObj));
        }
        ListData->SetArrayField(TEXT("graphs"), List);
        return FUnrealMCPCommonUtils::CreateSuccessResponse(ListData);
    }

    const FMCPGraphEntry* Found = AllGraphs.FindByPredicate([&GraphName](const FMCPGraphEntry& Entry)
    {
        return Entry.Graph && Entry.Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase);
    });

    if (!Found)
    {
        FString Available;
        for (const FMCPGraphEntry& Entry : AllGraphs)
        {
            Available += FString::Printf(TEXT("%s (%s), "), *Entry.Graph->GetName(), *Entry.Category);
        }
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph '%s' not found. Available: %s"), *GraphName, *Available));
    }

    UEdGraph* Target = Found->Graph;
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("blueprint"), Blueprint->GetName());
    Data->SetStringField(TEXT("graph"), Target->GetName());
    Data->SetStringField(TEXT("category"), Found->Category);
    Data->SetNumberField(TEXT("num_nodes"), Target->Nodes.Num());

    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (UEdGraphNode* Node : Target->Nodes)
    {
        if (!Node)
        {
            continue;
        }
        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
        NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());

        // Resolve call targets so a trace reads as real calls, not node classes.
        if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
        {
            if (UFunction* Func = CallNode->GetTargetFunction())
            {
                NodeObj->SetStringField(TEXT("target_function"), Func->GetName());
                if (UClass* Owner = Func->GetOwnerClass())
                {
                    NodeObj->SetStringField(TEXT("target_class"), Owner->GetName());
                }
            }
            else
            {
                // GetTargetFunction can be null pre-compile; fall back to the reference.
                const FName MemberName = CallNode->FunctionReference.GetMemberName();
                if (MemberName != NAME_None)
                {
                    NodeObj->SetStringField(TEXT("target_function"), MemberName.ToString());
                }
            }
        }
        else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
        {
            if (UEdGraph* MacroGraph = MacroNode->GetMacroGraph())
            {
                NodeObj->SetStringField(TEXT("target_macro"), MacroGraph->GetName());
            }
        }

        TArray<TSharedPtr<FJsonValue>> Pins;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }
            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
            PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"));
            PinObj->SetStringField(TEXT("type"), MCPPinTypeToString(Pin->PinType));
            if (!Pin->DefaultValue.IsEmpty())
            {
                PinObj->SetStringField(TEXT("default"), Pin->DefaultValue);
            }

            TArray<TSharedPtr<FJsonValue>> Links;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (!Linked || !Linked->GetOwningNode())
                {
                    continue;
                }
                TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
                LinkObj->SetStringField(TEXT("node"), Linked->GetOwningNode()->NodeGuid.ToString());
                LinkObj->SetStringField(TEXT("pin"), Linked->PinName.ToString());
                Links.Add(MakeShared<FJsonValueObject>(LinkObj));
            }
            if (Links.Num() > 0)
            {
                PinObj->SetArrayField(TEXT("links"), Links);
            }
            Pins.Add(MakeShared<FJsonValueObject>(PinObj));
        }
        NodeObj->SetArrayField(TEXT("pins"), Pins);
        Nodes.Add(MakeShared<FJsonValueObject>(NodeObj));
    }
    Data->SetArrayField(TEXT("nodes"), Nodes);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleDiffBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString BasePath;
    if (!Params->TryGetStringField(TEXT("base_version_path"), BasePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'base_version_path' parameter (a .uasset of the revision to compare against)"));
    }
    FString GraphFilter;
    Params->TryGetStringField(TEXT("graph_name"), GraphFilter);

    UBlueprint* CurrentBP = MCPFindBlueprintFlexible(BlueprintName);
    if (!CurrentBP)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }
    UBlueprint* BaseBP = MCPLoadBlueprintForDiff(BasePath);
    if (!BaseBP)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Could not load base version as a blueprint: %s"), *BasePath));
    }

    TMap<FString, UEdGraph*> BaseByName;
    for (const FMCPGraphEntry& Entry : MCPCollectAllGraphs(BaseBP))
    {
        if (Entry.Graph) { BaseByName.Add(Entry.Graph->GetName(), Entry.Graph); }
    }
    TMap<FString, UEdGraph*> CurrentByName;
    for (const FMCPGraphEntry& Entry : MCPCollectAllGraphs(CurrentBP))
    {
        if (Entry.Graph) { CurrentByName.Add(Entry.Graph->GetName(), Entry.Graph); }
    }

    TArray<TSharedPtr<FJsonValue>> Diffs;
    int32 GraphsCompared = 0;

    auto EmitGraphPresence = [&Diffs](const FString& GraphName, const TCHAR* Category, const FString& Display)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("graph"), GraphName);
        Entry->SetStringField(TEXT("category"), Category);
        Entry->SetStringField(TEXT("display"), Display);
        Diffs.Add(MakeShared<FJsonValueObject>(Entry));
    };

    // Diff graphs present in the current asset (base -> current).
    for (const TPair<FString, UEdGraph*>& Pair : CurrentByName)
    {
        const FString& Name = Pair.Key;
        if (!GraphFilter.IsEmpty() && !Name.Equals(GraphFilter, ESearchCase::IgnoreCase)) { continue; }
        UEdGraph** BaseGraph = BaseByName.Find(Name);
        if (!BaseGraph)
        {
            EmitGraphPresence(Name, TEXT("addition"), FString::Printf(TEXT("Graph '%s' added"), *Name));
            continue;
        }
        ++GraphsCompared;
        TArray<FDiffSingleResult> Results;
        FGraphDiffControl::DiffGraphs(*BaseGraph, Pair.Value, Results);
        for (const FDiffSingleResult& Result : Results)
        {
            if (Result.Diff == EDiffType::NO_DIFFERENCE) { continue; }
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("graph"), Name);
            Entry->SetStringField(TEXT("category"), MCPDiffCategoryToString(Result.Category));
            Entry->SetNumberField(TEXT("type_id"), (int32)Result.Diff);
            Entry->SetStringField(TEXT("display"), Result.DisplayString.ToString());
            if (!Result.ToolTip.IsEmpty()) { Entry->SetStringField(TEXT("tooltip"), Result.ToolTip.ToString()); }
            if (Result.Node1) { Entry->SetStringField(TEXT("base_node"), Result.Node1->NodeGuid.ToString()); }
            if (Result.Node2) { Entry->SetStringField(TEXT("current_node"), Result.Node2->NodeGuid.ToString()); }
            if (Result.Pin1) { Entry->SetStringField(TEXT("base_pin"), Result.Pin1->PinName.ToString()); }
            if (Result.Pin2) { Entry->SetStringField(TEXT("current_pin"), Result.Pin2->PinName.ToString()); }
            Diffs.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    // Graphs that existed in the base but were removed.
    for (const TPair<FString, UEdGraph*>& Pair : BaseByName)
    {
        const FString& Name = Pair.Key;
        if (!GraphFilter.IsEmpty() && !Name.Equals(GraphFilter, ESearchCase::IgnoreCase)) { continue; }
        if (!CurrentByName.Contains(Name))
        {
            EmitGraphPresence(Name, TEXT("subtraction"), FString::Printf(TEXT("Graph '%s' removed"), *Name));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("blueprint"), CurrentBP->GetName());
    Data->SetStringField(TEXT("base"), BasePath);
    Data->SetNumberField(TEXT("graphs_compared"), GraphsCompared);
    Data->SetNumberField(TEXT("num_differences"), Diffs.Num());
    Data->SetArrayField(TEXT("differences"), Diffs);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleDiffAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetName;
    Params->TryGetStringField(TEXT("asset_name"), AssetName);
    FString CurrentPath;
    Params->TryGetStringField(TEXT("current_version_path"), CurrentPath);
    FString BasePath;
    if (!Params->TryGetStringField(TEXT("base_version_path"), BasePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'base_version_path' parameter"));
    }
    if (AssetName.IsEmpty() && CurrentPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Provide 'asset_name' (a live asset) or 'current_version_path' (a .uasset file)"));
    }
    FString GraphFilter;
    Params->TryGetStringField(TEXT("graph_name"), GraphFilter);

    // Current side: a live asset by name, or a .uasset file (needed for World Partition
    // external actors, which aren't resolvable as live assets).
    UObject* Current = CurrentPath.IsEmpty() ? MCPFindAssetFlexible(AssetName) : MCPLoadAssetForDiff(CurrentPath);
    if (!Current)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Could not resolve current asset: %s"), *(CurrentPath.IsEmpty() ? AssetName : CurrentPath)));
    }
    UObject* Base = MCPLoadAssetForDiff(BasePath);
    if (!Base)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Could not load base version: %s"), *BasePath));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset"), Current->GetName());
    Data->SetStringField(TEXT("asset_type"), Current->GetClass()->GetName());
    Data->SetStringField(TEXT("base"), BasePath);

    UBlueprint* CurrentBP = Cast<UBlueprint>(Current);
    UBlueprint* BaseBP = Cast<UBlueprint>(Base);

    // Graph diffs: blueprints only.
    if (CurrentBP && BaseBP)
    {
        TArray<TSharedPtr<FJsonValue>> GraphDiffs;
        int32 GraphsCompared = 0;
        MCPAppendBlueprintGraphDiffs(BaseBP, CurrentBP, GraphFilter, GraphDiffs, GraphsCompared);
        Data->SetNumberField(TEXT("graphs_compared"), GraphsCompared);
        Data->SetNumberField(TEXT("num_graph_differences"), GraphDiffs.Num());
        Data->SetArrayField(TEXT("graph_differences"), GraphDiffs);
    }

    // Property diffs: the CDO for blueprints, the asset itself for everything else.
    UObject* PropA = Base;
    UObject* PropB = Current;
    if (CurrentBP && BaseBP)
    {
        PropA = BaseBP->GeneratedClass ? BaseBP->GeneratedClass->GetDefaultObject() : nullptr;
        PropB = CurrentBP->GeneratedClass ? CurrentBP->GeneratedClass->GetDefaultObject() : nullptr;
    }
    TArray<TSharedPtr<FJsonValue>> PropDiffs;
    if (PropA && PropB && !Cast<UWorld>(Current))
    {
        TArray<FSingleObjectDiffEntry> Entries;
        DiffUtils::CompareUnrelatedObjects(PropA, PropB, Entries);
        for (const FSingleObjectDiffEntry& Entry : Entries)
        {
            if (MCPIsComponentRef(Entry.Identifier, PropB)) { continue; }
            TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
            Obj->SetStringField(TEXT("property"), Entry.Identifier.ToDisplayName());
            Obj->SetStringField(TEXT("change"), MCPPropertyDiffTypeToString(Entry.DiffType));
            if (Entry.DiffType != EPropertyDiffType::PropertyAddedToB)
            {
                Obj->SetStringField(TEXT("base_value"), MCPExportResolvedValue(Entry.Identifier, PropA));
            }
            if (Entry.DiffType != EPropertyDiffType::PropertyAddedToA)
            {
                Obj->SetStringField(TEXT("current_value"), MCPExportResolvedValue(Entry.Identifier, PropB));
            }
            PropDiffs.Add(MakeShared<FJsonValueObject>(Obj));
        }
    }
    Data->SetNumberField(TEXT("num_property_differences"), PropDiffs.Num());
    Data->SetArrayField(TEXT("property_differences"), PropDiffs);

    // Actors: pair components and diff each (transform, mesh, materials, add/remove) —
    // CompareUnrelatedObjects above skips component subobjects.
    if (AActor* CurActor = Cast<AActor>(Current))
    {
        if (AActor* BaseActor = Cast<AActor>(Base))
        {
            TArray<TSharedPtr<FJsonValue>> CompDiffs;
            MCPAppendActorComponentDiffs(BaseActor, CurActor, FString(), CompDiffs);
            Data->SetNumberField(TEXT("num_component_differences"), CompDiffs.Num());
            Data->SetArrayField(TEXT("component_differences"), CompDiffs);
        }
    }
    // Maps: world-settings diff + per-actor diffs of the persistent level.
    else if (UWorld* CurWorld = Cast<UWorld>(Current))
    {
        if (UWorld* BaseWorld = Cast<UWorld>(Base))
        {
            TArray<TSharedPtr<FJsonValue>> WorldSettingsDiffs;
            AWorldSettings* BaseWS = BaseWorld->GetWorldSettings(false, false);
            AWorldSettings* CurWS = CurWorld->GetWorldSettings(false, false);
            if (BaseWS && CurWS)
            {
                TArray<FSingleObjectDiffEntry> Entries;
                DiffUtils::CompareUnrelatedObjects(BaseWS, CurWS, Entries);
                for (const FSingleObjectDiffEntry& Entry : Entries)
                {
                    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
                    Obj->SetStringField(TEXT("property"), Entry.Identifier.ToDisplayName());
                    Obj->SetStringField(TEXT("change"), MCPPropertyDiffTypeToString(Entry.DiffType));
                    if (Entry.DiffType != EPropertyDiffType::PropertyAddedToB) { Obj->SetStringField(TEXT("base_value"), MCPExportResolvedValue(Entry.Identifier, BaseWS)); }
                    if (Entry.DiffType != EPropertyDiffType::PropertyAddedToA) { Obj->SetStringField(TEXT("current_value"), MCPExportResolvedValue(Entry.Identifier, CurWS)); }
                    WorldSettingsDiffs.Add(MakeShared<FJsonValueObject>(Obj));
                }
            }
            Data->SetArrayField(TEXT("world_settings_differences"), WorldSettingsDiffs);

            TArray<TSharedPtr<FJsonValue>> ActorDiffs;
            MCPDiffLevelActors(BaseWorld, CurWorld, ActorDiffs);
            Data->SetNumberField(TEXT("num_actor_differences"), ActorDiffs.Num());
            Data->SetArrayField(TEXT("actor_differences"), ActorDiffs);
        }
    }

    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetAssetInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetName;
    if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
    }
    UObject* Asset = MCPFindAssetFlexible(AssetName);
    if (!Asset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetName));
    }

    // Blueprint → reuse the rich blueprint introspection.
    if (Cast<UBlueprint>(Asset))
    {
        TSharedPtr<FJsonObject> Forward = MakeShared<FJsonObject>();
        Forward->SetStringField(TEXT("blueprint_name"), AssetName);
        return HandleGetBlueprintInfo(Forward);
    }

    // Any other asset → type + editable property dump (settings).
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Asset->GetName());
    Data->SetStringField(TEXT("path"), Asset->GetPathName());
    Data->SetStringField(TEXT("asset_type"), Asset->GetClass()->GetName());

    TArray<TSharedPtr<FJsonValue>> Supers;
    for (UClass* Super = Asset->GetClass()->GetSuperClass(); Super; Super = Super->GetSuperClass())
    {
        Supers.Add(MakeShared<FJsonValueString>(Super->GetName()));
    }
    Data->SetArrayField(TEXT("parent_classes"), Supers);

    TArray<TSharedPtr<FJsonValue>> Props;
    for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
    {
        FProperty* Prop = *It;
        if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
        {
            continue;
        }
        FString Value;
        Prop->ExportTextItem_Direct(Value, Prop->ContainerPtrToValuePtr<void>(Asset), nullptr, nullptr, PPF_None);
        if (Value.Len() > 240)
        {
            Value = Value.Left(240) + TEXT("…");
        }
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), Prop->GetName());
        Obj->SetStringField(TEXT("type"), Prop->GetCPPType());
        Obj->SetStringField(TEXT("value"), Value);
        Props.Add(MakeShared<FJsonValueObject>(Obj));
    }
    Data->SetArrayField(TEXT("properties"), Props);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleAnalyzeTrace(const TSharedPtr<FJsonObject>& Params)
{
    FString TracePath;
    if (!Params->TryGetStringField(TEXT("trace_path"), TracePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'trace_path' parameter"));
    }
    int32 Top = 25;
    {
        double TopD = 25.0;
        if (Params->TryGetNumberField(TEXT("top"), TopD)) { Top = FMath::Max(1, (int32)TopD); }
    }
    if (!FPaths::FileExists(TracePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Trace file not found: %s"), *TracePath));
    }

    ITraceServicesModule& Module = FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices"));
    TSharedPtr<TraceServices::IAnalysisService> Service = Module.GetAnalysisService();
    if (!Service.IsValid())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("TraceServices analysis service unavailable"));
    }
    // The "Asset Loading" (LoadTimeProfiler) module is opt-in (ShouldBeEnabledByDefault()
    // returns false), so its provider is absent from Analyze() unless we enable it first.
    {
        FString SectionsPre;
        Params->TryGetStringField(TEXT("sections"), SectionsPre);
        const bool bWantLoad = SectionsPre.IsEmpty() || SectionsPre == TEXT("all") || SectionsPre.Contains(TEXT("loadtime"));
        if (bWantLoad)
        {
            TSharedPtr<TraceServices::IModuleService> ModuleService = Module.GetModuleService();
            if (ModuleService.IsValid()) { ModuleService->SetModuleEnabled(FName(TEXT("TraceModule_LoadTimeProfiler")), true); }
        }
    }
    // Loads, analyzes, and waits for completion.
    TSharedPtr<const TraceServices::IAnalysisSession> Session = Service->Analyze(*TracePath);
    if (!Session.IsValid())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to analyze trace: %s"), *TracePath));
    }

    TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
    const double Duration = Session->GetDurationSeconds();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("trace"), TracePath);
    Data->SetNumberField(TEXT("duration_seconds"), Duration);

    auto FrameStats = [](TArray<double>& Ms) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
        const int32 N = Ms.Num();
        O->SetNumberField(TEXT("count"), N);
        if (N == 0) { return O; }
        Ms.Sort();
        double Sum = 0.0; for (double D : Ms) { Sum += D; }
        auto Pct = [&Ms, N](double P) { return Ms[FMath::Clamp((int32)FMath::RoundToInt((P / 100.0) * (N - 1)), 0, N - 1)]; };
        O->SetNumberField(TEXT("avg_ms"), Sum / N);
        O->SetNumberField(TEXT("min_ms"), Ms[0]);
        O->SetNumberField(TEXT("max_ms"), Ms[N - 1]);
        O->SetNumberField(TEXT("p50_ms"), Pct(50));
        O->SetNumberField(TEXT("p90_ms"), Pct(90));
        O->SetNumberField(TEXT("p99_ms"), Pct(99));
        O->SetNumberField(TEXT("avg_fps"), Sum > 0.0 ? (N * 1000.0 / Sum) : 0.0);
        return O;
    };

    const TraceServices::IFrameProvider& Frames = TraceServices::ReadFrameProvider(*Session);
    TSharedPtr<FJsonObject> FramesObj = MakeShared<FJsonObject>();
    const ETraceFrameType FrameTypes[2] = { TraceFrameType_Game, TraceFrameType_Rendering };
    const TCHAR* FrameNames[2] = { TEXT("game"), TEXT("rendering") };
    for (int32 i = 0; i < 2; ++i)
    {
        TArray<double> Durations;
        Frames.EnumerateFrames(FrameTypes[i], 0, Frames.GetFrameCount(FrameTypes[i]), [&Durations](const TraceServices::FFrame& F)
        {
            // Skip the trailing incomplete frame (open at trace end → EndTime is non-finite).
            const double D = (F.EndTime - F.StartTime) * 1000.0;
            if (F.EndTime > F.StartTime && FMath::IsFinite(D)) { Durations.Add(D); }
        });
        FramesObj->SetObjectField(FrameNames[i], FrameStats(Durations));
    }
    Data->SetObjectField(TEXT("frames"), FramesObj);

    const TraceServices::ITimingProfilerProvider* Timing = TraceServices::ReadTimingProfilerProvider(*Session);
    if (Timing)
    {
        TraceServices::FCreateAggregationParams AggParams;
        AggParams.IntervalStart = 0.0;
        AggParams.IntervalEnd = (Duration > 0.0) ? Duration : DBL_MAX;
        AggParams.CpuThreadFilter = [](uint32) { return true; };
        AggParams.GpuQueueFilter = [](uint32) { return true; };
        AggParams.SortBy = TraceServices::FCreateAggregationParams::ESortBy::TotalInclusiveTime;
        AggParams.SortOrder = TraceServices::FCreateAggregationParams::ESortOrder::Descending;
        AggParams.TableEntryLimit = Top * 6;

        TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>* Table = Timing->CreateAggregation(AggParams);
        if (Table)
        {
            TArray<TSharedPtr<FJsonValue>> CpuTimers;
            TArray<TSharedPtr<FJsonValue>> GpuTimers;
            TraceServices::ITableReader<TraceServices::FTimingProfilerAggregatedStats>* Reader = Table->CreateReader();
            for (; Reader && Reader->IsValid(); Reader->NextRow())
            {
                const TraceServices::FTimingProfilerAggregatedStats* Row = Reader->GetCurrentRow();
                if (!Row || !Row->Timer) { continue; }
                TArray<TSharedPtr<FJsonValue>>& Dst = Row->Timer->IsGpuTimer ? GpuTimers : CpuTimers;
                if (Dst.Num() >= Top) { continue; }
                TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
                E->SetStringField(TEXT("name"), Row->Timer->Name ? Row->Timer->Name : TEXT("?"));
                E->SetNumberField(TEXT("total_ms"), FMath::IsFinite(Row->TotalInclusiveTime) ? Row->TotalInclusiveTime * 1000.0 : 0.0);
                E->SetNumberField(TEXT("avg_ms"), FMath::IsFinite(Row->AverageInclusiveTime) ? Row->AverageInclusiveTime * 1000.0 : 0.0);
                E->SetNumberField(TEXT("max_ms"), FMath::IsFinite(Row->MaxInclusiveTime) ? Row->MaxInclusiveTime * 1000.0 : 0.0);
                E->SetNumberField(TEXT("count"), (double)Row->InstanceCount);
                Dst.Add(MakeShared<FJsonValueObject>(E));
            }
            delete Reader;
            delete Table;
            Data->SetArrayField(TEXT("top_cpu_timers"), CpuTimers);
            Data->SetArrayField(TEXT("top_gpu_timers"), GpuTimers);
        }
    }

    // Optional extra sections (heavier). 'sections' = "all" (default) or a comma list of
    // counters,memory,regions,bookmarks,loadtime.
    FString Sections;
    if (!Params->TryGetStringField(TEXT("sections"), Sections) || Sections.IsEmpty()) { Sections = TEXT("all"); }
    auto Want = [&Sections](const TCHAR* Name) { return Sections == TEXT("all") || Sections.Contains(Name); };

    if (Want(TEXT("counters")))
    {
        struct FCounterStat { FString Name; FString Group; double Min; double Max; double Avg; double Last; };
        TArray<FCounterStat> Stats;
        const TraceServices::ICounterProvider& CounterProvider = TraceServices::ReadCounterProvider(*Session);
        CounterProvider.EnumerateCounters([&Stats, Duration](uint32, const TraceServices::ICounter& Counter)
        {
            double Min = DBL_MAX, Max = -DBL_MAX, Sum = 0.0, Last = 0.0;
            int64 Count = 0;
            auto Visit = [&](double, double V) { Min = FMath::Min(Min, V); Max = FMath::Max(Max, V); Sum += V; Last = V; ++Count; };
            if (Counter.IsFloatingPoint())
            {
                Counter.EnumerateFloatValues(0.0, Duration, true, [&Visit](double T, double V) { Visit(T, V); });
            }
            else
            {
                Counter.EnumerateValues(0.0, Duration, true, [&Visit](double T, int64 V) { Visit(T, (double)V); });
            }
            if (Count == 0) { return; }
            Stats.Add({ Counter.GetName() ? Counter.GetName() : TEXT("?"), Counter.GetGroup() ? Counter.GetGroup() : TEXT(""), Min, Max, Sum / Count, Last });
        });
        Stats.Sort([](const FCounterStat& A, const FCounterStat& B) { return A.Max > B.Max; });
        TArray<TSharedPtr<FJsonValue>> Out;
        for (int32 i = 0; i < Stats.Num() && i < Top; ++i)
        {
            TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
            E->SetStringField(TEXT("name"), Stats[i].Name);
            if (!Stats[i].Group.IsEmpty()) { E->SetStringField(TEXT("group"), Stats[i].Group); }
            E->SetNumberField(TEXT("min"), Stats[i].Min);
            E->SetNumberField(TEXT("max"), Stats[i].Max);
            E->SetNumberField(TEXT("avg"), Stats[i].Avg);
            E->SetNumberField(TEXT("last"), Stats[i].Last);
            Out.Add(MakeShared<FJsonValueObject>(E));
        }
        Data->SetArrayField(TEXT("counters"), Out);
    }

    if (Want(TEXT("memory")))
    {
        const TraceServices::IMemoryProvider* Mem = TraceServices::ReadMemoryProvider(*Session);
        if (Mem)
        {
            Mem->BeginRead();
            TraceServices::FMemoryTrackerId TrackerId = 0;
            FString TrackerName;
            bool bHaveTracker = false;
            Mem->EnumerateTrackers([&](const TraceServices::FMemoryTrackerInfo& T)
            {
                if (!bHaveTracker || T.Name == TEXT("Default")) { TrackerId = T.Id; TrackerName = T.Name; bHaveTracker = true; }
            });
            struct FTagMem { FString Name; double CurrentMB; double PeakMB; };
            TArray<FTagMem> Tags;
            if (bHaveTracker)
            {
                Mem->EnumerateTags([&](const TraceServices::FMemoryTagInfo& Tag)
                {
                    if (Tags.Num() > 4096) { return; }
                    int64 Last = 0, Peak = 0;
                    bool bAny = false;
                    Mem->EnumerateTagSamples(TrackerId, Tag.Id, 0.0, Duration, false, [&](double, double, const TraceServices::FMemoryTagSample& S)
                    {
                        Last = S.Value; if (S.Value > Peak) { Peak = S.Value; } bAny = true;
                    });
                    if (bAny) { Tags.Add({ Tag.Name, Last / (1024.0 * 1024.0), Peak / (1024.0 * 1024.0) }); }
                });
            }
            Mem->EndRead();
            Tags.Sort([](const FTagMem& A, const FTagMem& B) { return A.PeakMB > B.PeakMB; });
            TArray<TSharedPtr<FJsonValue>> Out;
            for (int32 i = 0; i < Tags.Num() && i < Top; ++i)
            {
                TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
                E->SetStringField(TEXT("tag"), Tags[i].Name);
                E->SetNumberField(TEXT("current_mb"), Tags[i].CurrentMB);
                E->SetNumberField(TEXT("peak_mb"), Tags[i].PeakMB);
                Out.Add(MakeShared<FJsonValueObject>(E));
            }
            TSharedPtr<FJsonObject> MemObj = MakeShared<FJsonObject>();
            MemObj->SetStringField(TEXT("tracker"), TrackerName);
            MemObj->SetArrayField(TEXT("tags_by_peak"), Out);
            Data->SetObjectField(TEXT("memory_llm"), MemObj);
        }
    }

    if (Want(TEXT("regions")))
    {
        const TraceServices::IRegionProvider& RegionProvider = TraceServices::ReadRegionProvider(*Session);
        TraceServices::FProviderReadScopeLock RegionScope(RegionProvider);
        TArray<TSharedPtr<FJsonValue>> Out;
        RegionProvider.GetDefaultTimeline().EnumerateRegions(0.0, Duration, [&Out, Top](const TraceServices::FTimeRegion& R) -> bool
        {
            if (Out.Num() >= Top) { return false; }
            TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
            E->SetStringField(TEXT("name"), R.Text ? R.Text : TEXT("?"));
            E->SetNumberField(TEXT("begin_s"), R.BeginTime);
            const double Dur = (FMath::IsFinite(R.EndTime) && R.EndTime > R.BeginTime) ? (R.EndTime - R.BeginTime) : 0.0;
            E->SetNumberField(TEXT("duration_ms"), Dur * 1000.0);
            Out.Add(MakeShared<FJsonValueObject>(E));
            return true;
        });
        Data->SetArrayField(TEXT("regions"), Out);
    }

    if (Want(TEXT("bookmarks")))
    {
        const TraceServices::IBookmarkProvider& BookmarkProvider = TraceServices::ReadBookmarkProvider(*Session);
        TArray<TSharedPtr<FJsonValue>> Out;
        const int32 Cap = Top * 4;
        BookmarkProvider.EnumerateBookmarks(0.0, Duration, [&Out, Cap](const TraceServices::FBookmark& B)
        {
            if (Out.Num() >= Cap) { return; }
            TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
            E->SetNumberField(TEXT("time_s"), B.Time);
            E->SetStringField(TEXT("text"), B.Text ? B.Text : TEXT("?"));
            Out.Add(MakeShared<FJsonValueObject>(E));
        });
        Data->SetArrayField(TEXT("bookmarks"), Out);
    }

    if (Want(TEXT("loadtime")))
    {
        struct FLoadEvt { FString Name; double TotalMs; uint64 Count; };
        auto ReadAgg = [Top](TraceServices::ITable<TraceServices::FLoadTimeProfilerAggregatedStats>* Table, TArray<FLoadEvt>& Events)
        {
            if (!Table) { return; }
            TraceServices::ITableReader<TraceServices::FLoadTimeProfilerAggregatedStats>* Reader = Table->CreateReader();
            for (; Reader && Reader->IsValid(); Reader->NextRow())
            {
                const TraceServices::FLoadTimeProfilerAggregatedStats* Row = Reader->GetCurrentRow();
                if (Row && FMath::IsFinite(Row->Total)) { Events.Add({ Row->Name ? Row->Name : TEXT("?"), Row->Total * 1000.0, Row->Count }); }
            }
            delete Reader;
            delete Table;
        };

        TArray<TSharedPtr<FJsonValue>> Out;
        FString Status;
        const TraceServices::ILoadTimeProfilerProvider* Load = TraceServices::ReadLoadTimeProfilerProvider(*Session);
        if (!Load)
        {
            Status = TEXT("no load-time provider in session (loadtime channel was not traced)");
        }
        else
        {
            const uint64 Timelines = Load->GetTimelineCount();
            TArray<FLoadEvt> Events;
            ReadAgg(Load->CreateEventAggregation(0.0, Duration), Events);
            FString Grouping = TEXT("event");
            if (Events.Num() == 0)  // event aggregation empty -> fall back to object-type aggregation
            {
                ReadAgg(Load->CreateObjectTypeAggregation(0.0, Duration), Events);
                Grouping = TEXT("object_type");
            }
            Events.Sort([](const FLoadEvt& A, const FLoadEvt& B) { return A.TotalMs > B.TotalMs; });
            for (int32 i = 0; i < Events.Num() && i < Top; ++i)
            {
                TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
                E->SetStringField(TEXT("name"), Events[i].Name);
                E->SetNumberField(TEXT("total_ms"), Events[i].TotalMs);
                E->SetNumberField(TEXT("count"), (double)Events[i].Count);
                Out.Add(MakeShared<FJsonValueObject>(E));
            }
            if (Out.Num() == 0)
            {
                Status = FString::Printf(TEXT("provider present, %llu timelines, but no aggregated load events in [0,%.1fs] - trace likely captured no async package loads in window"), (unsigned long long)Timelines, Duration);
            }
            else
            {
                Status = FString::Printf(TEXT("ok (%llu timelines, grouped by %s)"), (unsigned long long)Timelines, *Grouping);
            }
        }
        Data->SetArrayField(TEXT("loadtime_events"), Out);
        Data->SetStringField(TEXT("loadtime_status"), Status);
    }

    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCaptureMemreport(const TSharedPtr<FJsonObject>& Params)
{
    bool bFull = true;
    Params->TryGetBoolField(TEXT("full"), bFull);
    if (!GEngine)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No engine"));
    }
    const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProfilingDir() + TEXT("MemReports/"));

    // `memreport` defers the actual write to a later editor tick (MemReportDeferred), which
    // can't run while this command blocks the game thread — so we only trigger it here and
    // let the Python tool poll `dir` for the new .memreport file.
    const bool bTriggered = GEngine->Exec(GWorld, bFull ? TEXT("memreport -full") : TEXT("memreport"));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("triggered"), bTriggered);
    Data->SetBoolField(TEXT("deferred"), true);
    Data->SetStringField(TEXT("dir"), Dir);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleStartTrace(const TSharedPtr<FJsonObject>& Params)
{
    FString Channels;
    if (!Params->TryGetStringField(TEXT("channels"), Channels) || Channels.IsEmpty())
    {
        Channels = TEXT("cpu,gpu,frame,counters,stats");
    }
    FString Path;
    if (!Params->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
    {
        Path = FPaths::ConvertRelativePathToFull(FPaths::ProfilingDir() / TEXT("mcp_capture.utrace"));
    }
    const bool bOk = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *Path, *Channels, nullptr);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("started"), bOk);
    Data->SetBoolField(TEXT("connected"), FTraceAuxiliary::IsConnected());
    Data->SetStringField(TEXT("path"), Path);
    Data->SetStringField(TEXT("channels"), Channels);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleStopTrace(const TSharedPtr<FJsonObject>& Params)
{
    const FString Dest = FTraceAuxiliary::GetTraceDestinationString();
    const bool bOk = FTraceAuxiliary::Stop();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("stopped"), bOk);
    Data->SetStringField(TEXT("path"), Dest);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Check if blueprint already exists
    FString PackagePath = TEXT("/Game/Blueprints/");
    FString AssetName = BlueprintName;
    if (UEditorAssetLibrary::DoesAssetExist(PackagePath + AssetName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint already exists: %s"), *BlueprintName));
    }

    // Create the blueprint factory
    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    
    // Handle parent class
    FString ParentClass;
    Params->TryGetStringField(TEXT("parent_class"), ParentClass);
    
    // Default to Actor if no parent class specified
    UClass* SelectedParentClass = AActor::StaticClass();
    
    // Try to find the specified parent class
    if (!ParentClass.IsEmpty())
    {
        FString ClassName = ParentClass;
        if (!ClassName.StartsWith(TEXT("A")))
        {
            ClassName = TEXT("A") + ClassName;
        }
        
        // First try direct StaticClass lookup for common classes
        UClass* FoundClass = nullptr;
        if (ClassName == TEXT("APawn"))
        {
            FoundClass = APawn::StaticClass();
        }
        else if (ClassName == TEXT("AActor"))
        {
            FoundClass = AActor::StaticClass();
        }
        else
        {
            // Try loading the class using LoadClass which is more reliable than FindObject
            const FString ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
            FoundClass = LoadClass<AActor>(nullptr, *ClassPath);
            
            if (!FoundClass)
            {
                // Try alternate paths if not found
                const FString GameClassPath = FString::Printf(TEXT("/Script/Game.%s"), *ClassName);
                FoundClass = LoadClass<AActor>(nullptr, *GameClassPath);
            }
        }

        if (FoundClass)
        {
            SelectedParentClass = FoundClass;
            UE_LOG(LogTemp, Log, TEXT("Successfully set parent class to '%s'"), *ClassName);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find specified parent class '%s' at paths: /Script/Engine.%s or /Script/Game.%s, defaulting to AActor"), 
                *ClassName, *ClassName, *ClassName);
        }
    }
    
    Factory->ParentClass = SelectedParentClass;

    // Create the blueprint
    UPackage* Package = CreatePackage(*(PackagePath + AssetName));
    UBlueprint* NewBlueprint = Cast<UBlueprint>(Factory->FactoryCreateNew(UBlueprint::StaticClass(), Package, *AssetName, RF_Standalone | RF_Public, nullptr, GWarn));

    if (NewBlueprint)
    {
        // Notify the asset registry
        FAssetRegistryModule::AssetCreated(NewBlueprint);

        // Mark the package dirty
        Package->MarkPackageDirty();

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("name"), AssetName);
        ResultObj->SetStringField(TEXT("path"), PackagePath + AssetName);
        return ResultObj;
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create blueprint"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleAddComponentToBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentType;
    if (!Params->TryGetStringField(TEXT("component_type"), ComponentType))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Create the component - dynamically find the component class by name
    UClass* ComponentClass = nullptr;

    // Try to find the class with exact name first
    ComponentClass = FindObject<UClass>(ANY_PACKAGE, *ComponentType);
    
    // If not found, try with "Component" suffix
    if (!ComponentClass && !ComponentType.EndsWith(TEXT("Component")))
    {
        FString ComponentTypeWithSuffix = ComponentType + TEXT("Component");
        ComponentClass = FindObject<UClass>(ANY_PACKAGE, *ComponentTypeWithSuffix);
    }
    
    // If still not found, try with "U" prefix
    if (!ComponentClass && !ComponentType.StartsWith(TEXT("U")))
    {
        FString ComponentTypeWithPrefix = TEXT("U") + ComponentType;
        ComponentClass = FindObject<UClass>(ANY_PACKAGE, *ComponentTypeWithPrefix);
        
        // Try with both prefix and suffix
        if (!ComponentClass && !ComponentType.EndsWith(TEXT("Component")))
        {
            FString ComponentTypeWithBoth = TEXT("U") + ComponentType + TEXT("Component");
            ComponentClass = FindObject<UClass>(ANY_PACKAGE, *ComponentTypeWithBoth);
        }
    }
    
    // Verify that the class is a valid component type
    if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown component type: %s"), *ComponentType));
    }

    // Add the component to the blueprint
    USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, *ComponentName);
    if (NewNode)
    {
        // Set transform if provided
        USceneComponent* SceneComponent = Cast<USceneComponent>(NewNode->ComponentTemplate);
        if (SceneComponent)
        {
            if (Params->HasField(TEXT("location")))
            {
                SceneComponent->SetRelativeLocation(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
            }
            if (Params->HasField(TEXT("rotation")))
            {
                SceneComponent->SetRelativeRotation(FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
            }
            if (Params->HasField(TEXT("scale")))
            {
                SceneComponent->SetRelativeScale3D(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
            }
        }

        // Add to root if no parent specified
        Blueprint->SimpleConstructionScript->AddNode(NewNode);

        // Compile the blueprint
        FKismetEditorUtilities::CompileBlueprint(Blueprint);

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("component_name"), ComponentName);
        ResultObj->SetStringField(TEXT("component_type"), ComponentType);
        return ResultObj;
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to add component to blueprint"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    // Log all input parameters for debugging
    UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Blueprint: %s, Component: %s, Property: %s"), 
        *BlueprintName, *ComponentName, *PropertyName);
    
    // Log property_value if available
    if (Params->HasField(TEXT("property_value")))
    {
        TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
        FString ValueType;
        
        switch(JsonValue->Type)
        {
            case EJson::Boolean: ValueType = FString::Printf(TEXT("Boolean: %s"), JsonValue->AsBool() ? TEXT("true") : TEXT("false")); break;
            case EJson::Number: ValueType = FString::Printf(TEXT("Number: %f"), JsonValue->AsNumber()); break;
            case EJson::String: ValueType = FString::Printf(TEXT("String: %s"), *JsonValue->AsString()); break;
            case EJson::Array: ValueType = TEXT("Array"); break;
            case EJson::Object: ValueType = TEXT("Object"); break;
            default: ValueType = TEXT("Unknown"); break;
        }
        
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Value Type: %s"), *ValueType);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - No property_value provided"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Blueprint not found: %s"), *BlueprintName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Blueprint found: %s (Class: %s)"), 
            *BlueprintName, 
            Blueprint->GeneratedClass ? *Blueprint->GeneratedClass->GetName() : TEXT("NULL"));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Searching for component %s in blueprint nodes"), *ComponentName);
    
    if (!Blueprint->SimpleConstructionScript)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - SimpleConstructionScript is NULL for blueprint %s"), *BlueprintName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid blueprint construction script"));
    }
    
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node)
        {
            UE_LOG(LogTemp, Verbose, TEXT("SetComponentProperty - Found node: %s"), *Node->GetVariableName().ToString());
            if (Node->GetVariableName().ToString() == ComponentName)
            {
                ComponentNode = Node;
                break;
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Found NULL node in blueprint"));
        }
    }

    if (!ComponentNode)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Component not found: %s"), *ComponentName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Component found: %s (Class: %s)"), 
            *ComponentName, 
            ComponentNode->ComponentTemplate ? *ComponentNode->ComponentTemplate->GetClass()->GetName() : TEXT("NULL"));
    }

    // Get the component template
    UObject* ComponentTemplate = ComponentNode->ComponentTemplate;
    if (!ComponentTemplate)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Component template is NULL for %s"), *ComponentName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid component template"));
    }

    // Check if this is a Spring Arm component and log special debug info
    if (ComponentTemplate->GetClass()->GetName().Contains(TEXT("SpringArm")))
    {
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - SpringArm component detected! Class: %s"), 
            *ComponentTemplate->GetClass()->GetPathName());
            
        // Log all properties of the SpringArm component class
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - SpringArm properties:"));
        for (TFieldIterator<FProperty> PropIt(ComponentTemplate->GetClass()); PropIt; ++PropIt)
        {
            FProperty* Prop = *PropIt;
            UE_LOG(LogTemp, Warning, TEXT("  - %s (%s)"), *Prop->GetName(), *Prop->GetCPPType());
        }

        // Special handling for Spring Arm properties
        if (Params->HasField(TEXT("property_value")))
        {
            TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
            
            // Get the property using the new FField system
            FProperty* Property = FindFProperty<FProperty>(ComponentTemplate->GetClass(), *PropertyName);
            if (!Property)
            {
                UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Property %s not found on SpringArm component"), *PropertyName);
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Property %s not found on SpringArm component"), *PropertyName));
            }

            // Create a scope guard to ensure property cleanup
            struct FScopeGuard
            {
                UObject* Object;
                FScopeGuard(UObject* InObject) : Object(InObject) 
                {
                    if (Object)
                    {
                        Object->Modify();
                    }
                }
                ~FScopeGuard()
                {
                    if (Object)
                    {
                        Object->PostEditChange();
                    }
                }
            } ScopeGuard(ComponentTemplate);

            bool bSuccess = false;
            FString ErrorMessage;

            // Handle specific Spring Arm property types
            if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
            {
                if (JsonValue->Type == EJson::Number)
                {
                    const float Value = JsonValue->AsNumber();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting float property %s to %f"), *PropertyName, Value);
                    FloatProp->SetPropertyValue_InContainer(ComponentTemplate, Value);
                    bSuccess = true;
                }
            }
            else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
            {
                if (JsonValue->Type == EJson::Boolean)
                {
                    const bool Value = JsonValue->AsBool();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting bool property %s to %d"), *PropertyName, Value);
                    BoolProp->SetPropertyValue_InContainer(ComponentTemplate, Value);
                    bSuccess = true;
                }
            }
            else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
            {
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Handling struct property %s of type %s"), 
                    *PropertyName, *StructProp->Struct->GetName());
                
                // Special handling for common Spring Arm struct properties
                if (StructProp->Struct == TBaseStructure<FVector>::Get())
                {
                    if (JsonValue->Type == EJson::Array)
                    {
                        const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
                        if (Arr.Num() == 3)
                        {
                            FVector Vec(
                                Arr[0]->AsNumber(),
                                Arr[1]->AsNumber(),
                                Arr[2]->AsNumber()
                            );
                            void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                            StructProp->CopySingleValue(PropertyAddr, &Vec);
                            bSuccess = true;
                        }
                    }
                }
                else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
                {
                    if (JsonValue->Type == EJson::Array)
                    {
                        const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
                        if (Arr.Num() == 3)
                        {
                            FRotator Rot(
                                Arr[0]->AsNumber(),
                                Arr[1]->AsNumber(),
                                Arr[2]->AsNumber()
                            );
                            void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                            StructProp->CopySingleValue(PropertyAddr, &Rot);
                            bSuccess = true;
                        }
                    }
                }
            }

            if (bSuccess)
            {
                // Mark the blueprint as modified
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Successfully set SpringArm property %s"), *PropertyName);
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

                TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
                ResultObj->SetStringField(TEXT("component"), ComponentName);
                ResultObj->SetStringField(TEXT("property"), PropertyName);
                ResultObj->SetBoolField(TEXT("success"), true);
                return ResultObj;
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set SpringArm property %s"), *PropertyName);
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to set SpringArm property %s"), *PropertyName));
            }
        }
    }

    // Regular property handling for non-Spring Arm components continues...

    // Set the property value
    if (Params->HasField(TEXT("property_value")))
    {
        TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
        
        // Get the property
        FProperty* Property = FindFProperty<FProperty>(ComponentTemplate->GetClass(), *PropertyName);
        if (!Property)
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Property %s not found on component %s"), 
                *PropertyName, *ComponentName);
            
            // List all available properties for this component
            UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Available properties for %s:"), *ComponentName);
            for (TFieldIterator<FProperty> PropIt(ComponentTemplate->GetClass()); PropIt; ++PropIt)
            {
                FProperty* Prop = *PropIt;
                UE_LOG(LogTemp, Warning, TEXT("  - %s (%s)"), *Prop->GetName(), *Prop->GetCPPType());
            }
            
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Property %s not found on component %s"), *PropertyName, *ComponentName));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property found: %s (Type: %s)"), 
                *PropertyName, *Property->GetCPPType());
        }

        bool bSuccess = false;
        FString ErrorMessage;

        // Handle different property types
        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Attempting to set property %s"), *PropertyName);
        
        // Add try-catch block to catch and log any crashes
        try
        {
            if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
            {
                // Handle vector properties
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property is a struct: %s"), 
                    StructProp->Struct ? *StructProp->Struct->GetName() : TEXT("NULL"));
                    
                if (StructProp->Struct == TBaseStructure<FVector>::Get())
                {
                    if (JsonValue->Type == EJson::Array)
                    {
                        // Handle array input [x, y, z]
                        const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
                        if (Arr.Num() == 3)
                        {
                            FVector Vec(
                                Arr[0]->AsNumber(),
                                Arr[1]->AsNumber(),
                                Arr[2]->AsNumber()
                            );
                            void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting Vector(%f, %f, %f)"), 
                                Vec.X, Vec.Y, Vec.Z);
                            StructProp->CopySingleValue(PropertyAddr, &Vec);
                            bSuccess = true;
                        }
                        else
                        {
                            ErrorMessage = FString::Printf(TEXT("Vector property requires 3 values, got %d"), Arr.Num());
                            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                        }
                    }
                    else if (JsonValue->Type == EJson::Number)
                    {
                        // Handle scalar input (sets all components to same value)
                        float Value = JsonValue->AsNumber();
                        FVector Vec(Value, Value, Value);
                        void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting Vector(%f, %f, %f) from scalar"), 
                            Vec.X, Vec.Y, Vec.Z);
                        StructProp->CopySingleValue(PropertyAddr, &Vec);
                        bSuccess = true;
                    }
                    else
                    {
                        ErrorMessage = TEXT("Vector property requires either a single number or array of 3 numbers");
                        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                    }
                }
                else
                {
                    // Handle other struct properties using default handler
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Using generic struct handler for %s"), 
                        *PropertyName);
                    bSuccess = FUnrealMCPCommonUtils::SetObjectProperty(ComponentTemplate, PropertyName, JsonValue, ErrorMessage);
                    if (!bSuccess)
                    {
                        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set struct property: %s"), *ErrorMessage);
                    }
                }
            }
            else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
            {
                // Handle enum properties
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property is an enum"));
                if (JsonValue->Type == EJson::String)
                {
                    FString EnumValueName = JsonValue->AsString();
                    UEnum* Enum = EnumProp->GetEnum();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting enum from string: %s"), *EnumValueName);
                    
                    if (Enum)
                    {
                        int64 EnumValue = Enum->GetValueByNameString(EnumValueName);
                        
                        if (EnumValue != INDEX_NONE)
                        {
                            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Found enum value: %lld"), EnumValue);
                            EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(
                                ComponentTemplate, 
                                EnumValue
                            );
                            bSuccess = true;
                        }
                        else
                        {
                            // List all possible enum values
                            UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Available enum values for %s:"), 
                                *Enum->GetName());
                            for (int32 i = 0; i < Enum->NumEnums(); i++)
                            {
                                UE_LOG(LogTemp, Warning, TEXT("  - %s (%lld)"), 
                                    *Enum->GetNameStringByIndex(i),
                                    Enum->GetValueByIndex(i));
                            }
                            
                            ErrorMessage = FString::Printf(TEXT("Invalid enum value '%s' for property %s"), 
                                *EnumValueName, *PropertyName);
                            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                        }
                    }
                    else
                    {
                        ErrorMessage = TEXT("Enum object is NULL");
                        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                    }
                }
                else if (JsonValue->Type == EJson::Number)
                {
                    // Allow setting enum by integer value
                    int64 EnumValue = JsonValue->AsNumber();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting enum from number: %lld"), EnumValue);
                    EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(
                        ComponentTemplate, 
                        EnumValue
                    );
                    bSuccess = true;
                }
                else
                {
                    ErrorMessage = TEXT("Enum property requires either a string name or integer value");
                    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                }
            }
            else if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
            {
                // Handle numeric properties
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property is numeric: IsInteger=%d, IsFloat=%d"), 
                    NumericProp->IsInteger(), NumericProp->IsFloatingPoint());
                    
                if (JsonValue->Type == EJson::Number)
                {
                    double Value = JsonValue->AsNumber();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting numeric value: %f"), Value);
                    
                    if (NumericProp->IsInteger())
                    {
                        NumericProp->SetIntPropertyValue(ComponentTemplate, (int64)Value);
                        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Set integer value: %lld"), (int64)Value);
                        bSuccess = true;
                    }
                    else if (NumericProp->IsFloatingPoint())
                    {
                        NumericProp->SetFloatingPointPropertyValue(ComponentTemplate, Value);
                        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Set float value: %f"), Value);
                        bSuccess = true;
                    }
                }
                else
                {
                    ErrorMessage = TEXT("Numeric property requires a number value");
                    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                }
            }
            else
            {
                // Handle all other property types using default handler
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Using generic property handler for %s (Type: %s)"), 
                    *PropertyName, *Property->GetCPPType());
                bSuccess = FUnrealMCPCommonUtils::SetObjectProperty(ComponentTemplate, PropertyName, JsonValue, ErrorMessage);
                if (!bSuccess)
                {
                    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set property: %s"), *ErrorMessage);
                }
            }
        }
        catch (const std::exception& Ex)
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - EXCEPTION: %s"), ANSI_TO_TCHAR(Ex.what()));
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Exception while setting property %s: %s"), *PropertyName, ANSI_TO_TCHAR(Ex.what())));
        }
        catch (...)
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - UNKNOWN EXCEPTION occurred while setting property %s"), *PropertyName);
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown exception while setting property %s"), *PropertyName));
        }

        if (bSuccess)
        {
            // Mark the blueprint as modified
            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Successfully set property %s on component %s"), 
                *PropertyName, *ComponentName);
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetStringField(TEXT("component"), ComponentName);
            ResultObj->SetStringField(TEXT("property"), PropertyName);
            ResultObj->SetBoolField(TEXT("success"), true);
            return ResultObj;
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set property %s: %s"), 
                *PropertyName, *ErrorMessage);
            return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
        }
    }

    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Missing 'property_value' parameter"));
    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetPhysicsProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Set physics properties
    if (Params->HasField(TEXT("simulate_physics")))
    {
        PrimComponent->SetSimulatePhysics(Params->GetBoolField(TEXT("simulate_physics")));
    }

    if (Params->HasField(TEXT("mass")))
    {
        float Mass = Params->GetNumberField(TEXT("mass"));
        // In UE5.5, use proper overrideMass instead of just scaling
        PrimComponent->SetMassOverrideInKg(NAME_None, Mass);
        UE_LOG(LogTemp, Display, TEXT("Set mass for component %s to %f kg"), *ComponentName, Mass);
    }

    if (Params->HasField(TEXT("linear_damping")))
    {
        PrimComponent->SetLinearDamping(Params->GetNumberField(TEXT("linear_damping")));
    }

    if (Params->HasField(TEXT("angular_damping")))
    {
        PrimComponent->SetAngularDamping(Params->GetNumberField(TEXT("angular_damping")));
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Compile the blueprint
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), BlueprintName);
    ResultObj->SetBoolField(TEXT("compiled"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }

    // Spawn the actor
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));

    AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform);
    if (NewActor)
    {
        NewActor->SetActorLabel(*ActorName);
        return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to spawn blueprint actor"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetBlueprintProperty(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the default object
    UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
    if (!DefaultObject)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get default object"));
    }

    // Set the property value
    if (Params->HasField(TEXT("property_value")))
    {
        TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
        
        FString ErrorMessage;
        if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, PropertyName, JsonValue, ErrorMessage))
        {
            // Mark the blueprint as modified
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetStringField(TEXT("property"), PropertyName);
            ResultObj->SetBoolField(TEXT("success"), true);
            return ResultObj;
        }
        else
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
        }
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetStaticMeshProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentNode->ComponentTemplate);
    if (!MeshComponent)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a static mesh component"));
    }

    // Set static mesh properties
    if (Params->HasField(TEXT("static_mesh")))
    {
        FString MeshPath = Params->GetStringField(TEXT("static_mesh"));
        UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
        if (Mesh)
        {
            MeshComponent->SetStaticMesh(Mesh);
        }
    }

    if (Params->HasField(TEXT("material")))
    {
        FString MaterialPath = Params->GetStringField(TEXT("material"));
        UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
        if (Material)
        {
            MeshComponent->SetMaterial(0, Material);
        }
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetPawnProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the default object
    UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
    if (!DefaultObject)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get default object"));
    }

    // Track if any properties were set successfully
    bool bAnyPropertiesSet = false;
    TSharedPtr<FJsonObject> ResultsObj = MakeShared<FJsonObject>();
    
    // Set auto possess player if specified
    if (Params->HasField(TEXT("auto_possess_player")))
    {
        TSharedPtr<FJsonValue> AutoPossessValue = Params->Values.FindRef(TEXT("auto_possess_player"));
        
        FString ErrorMessage;
        if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, TEXT("AutoPossessPlayer"), AutoPossessValue, ErrorMessage))
        {
            bAnyPropertiesSet = true;
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), true);
            ResultsObj->SetObjectField(TEXT("AutoPossessPlayer"), PropResultObj);
        }
        else
        {
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), false);
            PropResultObj->SetStringField(TEXT("error"), ErrorMessage);
            ResultsObj->SetObjectField(TEXT("AutoPossessPlayer"), PropResultObj);
        }
    }
    
    // Set controller rotation properties
    const TCHAR* RotationProps[] = {
        TEXT("bUseControllerRotationYaw"),
        TEXT("bUseControllerRotationPitch"),
        TEXT("bUseControllerRotationRoll")
    };
    
    const TCHAR* ParamNames[] = {
        TEXT("use_controller_rotation_yaw"),
        TEXT("use_controller_rotation_pitch"),
        TEXT("use_controller_rotation_roll")
    };
    
    for (int32 i = 0; i < 3; i++)
    {
        if (Params->HasField(ParamNames[i]))
        {
            TSharedPtr<FJsonValue> Value = Params->Values.FindRef(ParamNames[i]);
            
            FString ErrorMessage;
            if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, RotationProps[i], Value, ErrorMessage))
            {
                bAnyPropertiesSet = true;
                TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
                PropResultObj->SetBoolField(TEXT("success"), true);
                ResultsObj->SetObjectField(RotationProps[i], PropResultObj);
            }
            else
            {
                TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
                PropResultObj->SetBoolField(TEXT("success"), false);
                PropResultObj->SetStringField(TEXT("error"), ErrorMessage);
                ResultsObj->SetObjectField(RotationProps[i], PropResultObj);
            }
        }
    }
    
    // Set can be damaged property
    if (Params->HasField(TEXT("can_be_damaged")))
    {
        TSharedPtr<FJsonValue> Value = Params->Values.FindRef(TEXT("can_be_damaged"));
        
        FString ErrorMessage;
        if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, TEXT("bCanBeDamaged"), Value, ErrorMessage))
        {
            bAnyPropertiesSet = true;
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), true);
            ResultsObj->SetObjectField(TEXT("bCanBeDamaged"), PropResultObj);
        }
        else
        {
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), false);
            PropResultObj->SetStringField(TEXT("error"), ErrorMessage);
            ResultsObj->SetObjectField(TEXT("bCanBeDamaged"), PropResultObj);
        }
    }

    // Mark the blueprint as modified if any properties were set
    if (bAnyPropertiesSet)
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }
    else if (ResultsObj->Values.Num() == 0)
    {
        // No properties were specified
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No properties specified to set"));
    }

    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetStringField(TEXT("blueprint"), BlueprintName);
    ResponseObj->SetBoolField(TEXT("success"), bAnyPropertiesSet);
    ResponseObj->SetObjectField(TEXT("results"), ResultsObj);
    return ResponseObj;
} 