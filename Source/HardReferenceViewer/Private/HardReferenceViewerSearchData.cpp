﻿#include "HardReferenceViewerSearchData.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"

#if ENGINE_MAJOR_VERSION < 5
#include "SSCSEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#else
#include "SSubobjectEditor.h"
#endif

#define LOCTEXT_NAMESPACE "FHardReferenceViewerModule"

TArray<FHRVTreeViewItemPtr> FHardReferenceViewerSearchData::GatherSearchData(TWeakPtr<FBlueprintEditor> BlueprintEditor)
{
	Reset();

	TMap<FName, FHRVTreeViewItemPtr> DependentPackageMap;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	
	// Get this blueprints package dependencies from the blueprint editor 
	TArray<FName> PackageDependencies;
	GetPackageDependencies(PackageDependencies, SizeOnDisk, AssetRegistryModule, BlueprintEditor);

	// Populate display information from package dependencies
	{
		TMap<FName, FAssetData> DependencyToAssetDataMap;
		GetAssetForPackages(PackageDependencies, DependencyToAssetDataMap);

		for (auto MapIt = DependencyToAssetDataMap.CreateConstIterator(); MapIt; ++MapIt)
		{
			const FName& PathName = MapIt.Key();
			const FAssetData& AssetData = MapIt.Value();
			FString AssetTypeName = GetAssetTypeName(AssetData);
			FString FileName = FPaths::GetCleanFilename(PathName.ToString());
		
			FAssetPackageData AssetPackageData;
			TryGetAssetPackageData(PathName, AssetPackageData);

			if( FHRVTreeViewItemPtr Header = MakeShared<FHRVTreeViewItem>() )
			{
				Header->bIsHeader = true;
				Header->PackageId = PathName;
				Header->Tooltip = FText::FromName(PathName);
				Header->Name = FText::FromString(FileName);
				Header->SizeOnDisk = AssetPackageData.DiskSize;
				Header->SlateIcon = FSlateIcon("EditorStyle", FName( *("ClassIcon." + AssetTypeName))); 

				FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));	
				if (UClass* AssetClass = AssetData.GetClass())
				{
					TWeakPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(AssetData.GetClass());
					if(AssetTypeActions.IsValid())
					{
						Header->IconColor = AssetTypeActions.Pin()->GetTypeColor();
					}
				}
			
				DependentPackageMap.Add(PathName, Header);
				TreeView.Add(Header);
			}
		}
	}
	
	{
		// Search through blueprint nodes for references to the dependent packages
		if( UBlueprint* Blueprint = BlueprintEditor.Pin()->GetBlueprintObj() )
		{
			SearchGraphNodes(DependentPackageMap, AssetRegistryModule, Blueprint->UbergraphPages);
			SearchGraphNodes(DependentPackageMap, AssetRegistryModule, Blueprint->FunctionGraphs);
			SearchMemberVariables(DependentPackageMap, AssetRegistryModule, Blueprint);
			// @omidk todo: Search local function variables
			// @omidk todo: Search components
		}
	}

	// If we didn't discover any references to a package make a note
	for(FHRVTreeViewItemPtr HeaderItem : TreeView)
	{
		if(HeaderItem->Children.Num() <= 0)
		{
			FHRVTreeViewItemPtr ChildItem = MakeShared<FHRVTreeViewItem>();
			HeaderItem->Children.Add(ChildItem);
			ChildItem->Name = LOCTEXT("UnknownSource", "Unknown source");
		}
	}
	
	// sort from largest to smallest
	TreeView.Sort([](FHRVTreeViewItemPtr Lhs, FHRVTreeViewItemPtr Rhs)
	{
		return Lhs->SizeOnDisk > Rhs->SizeOnDisk;
	});
	
	return TreeView;
}

void FHardReferenceViewerSearchData::Reset()
{
	SizeOnDisk = 0;
	TreeView.Reset();
}

UObject* FHardReferenceViewerSearchData::GetObjectContext(TWeakPtr<FBlueprintEditor> BlueprintEditor) const
{
	if(!BlueprintEditor.IsValid())
	{
		return nullptr;
	}

#if ENGINE_MAJOR_VERSION < 5
	TSharedPtr<class SSCSEditor> SCSEditorPtr = BlueprintEditor.Pin()->GetSCSEditor();
	if(!SCSEditorPtr.IsValid())
	{
		return nullptr;
	}
	return SCSEditorPtr->GetActorContext();
#else
	TSharedPtr<SSubobjectEditor> SubobjectEditorPtr = BlueprintEditor.Pin()->GetSubobjectEditor();
	SSubobjectEditor* SubobjectEditorWidget = SubobjectEditorPtr.Get();
	if(SubobjectEditorWidget == nullptr)
	{
		return nullptr;
	}
		
	UObject* Object = SubobjectEditorWidget->GetObjectContext();
	return Object;
#endif
}

void FHardReferenceViewerSearchData::GetPackageDependencies(TArray<FName>& OutPackageDependencies, int& OutSizeOnDisk, FAssetRegistryModule& AssetRegistryModule, TWeakPtr<FBlueprintEditor> BlueprintEditor) const
{
	UObject* Object = GetObjectContext(BlueprintEditor);
	if(Object == nullptr)
	{
		return;
	}
	
	FAssetData ExistingAsset = GetAssetDataForObject(Object);

	UE::AssetRegistry::FDependencyQuery Flags(UE::AssetRegistry::EDependencyQuery::Hard);
	AssetRegistryModule.GetDependencies(ExistingAsset.PackageName, OutPackageDependencies, UE::AssetRegistry::EDependencyCategory::Package, Flags);
	
	FAssetPackageData AssetPackageData;
	TryGetAssetPackageData(ExistingAsset.PackageName, AssetPackageData);
	OutSizeOnDisk = AssetPackageData.DiskSize;
}

void FHardReferenceViewerSearchData::SearchGraphNodes(TMap<FName, FHRVTreeViewItemPtr>& OutPackageMap, const FAssetRegistryModule& AssetRegistryModule, const FEdGraphArray& EdGraphList) const
{
	for(UEdGraph* Graph : EdGraphList)
	{
		if(Graph)
		{
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				const UPackage* FunctionPackage = nullptr;
				if(const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
				{
					FunctionPackage = CallFunctionNode->FunctionReference.GetMemberParentPackage();
				}
				else if(const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
				{
					if(CastNode->TargetType)
					{
						FunctionPackage = CastNode->TargetType->GetPackage();
					}
				}

				if( const FHRVTreeViewItemPtr Result = CheckAddPackageResult(OutPackageMap, AssetRegistryModule, FunctionPackage) )
				{
					Result->Name = Node->GetNodeTitle(ENodeTitleType::ListView);
					Result->NodeGuid = Node->NodeGuid;
					Result->SlateIcon = Node->GetIconAndTint(Result->IconColor);
				}

				// Also search the pins of this node for any references to other packages, e.g. the 'Class' pin of a SpawnActor node.
				SearchNodePins(OutPackageMap, AssetRegistryModule, Node);
			}
		}
	}
}

void FHardReferenceViewerSearchData::SearchNodePins(TMap<FName, FHRVTreeViewItemPtr>& OutPackageMap, const FAssetRegistryModule& AssetRegistryModule, const UEdGraphNode* Node) const
{
	if(Node == nullptr)
	{
		return;
	}
	
	for(const UEdGraphPin* Pin : Node->Pins)
	{
		if(Pin->bHidden)
		{
			// skip hidden pins
			continue;
		}
					
		if(Pin->Direction == EGPD_Input)
		{
			if(const UObject* PinObject = Pin->DefaultObject)
			{
				const UPackage* FunctionPackage = PinObject->GetPackage();
				if( const FHRVTreeViewItemPtr Result = CheckAddPackageResult(OutPackageMap, AssetRegistryModule, FunctionPackage) )
				{
					Result->Name = FText::Format(LOCTEXT("FunctionInput","{0} ({1})"), FText::FromString(Pin->GetName()), Node->GetNodeTitle(ENodeTitleType::ListView));
					Result->NodeGuid = Node->NodeGuid;
					if( const UEdGraphSchema* Schema = Pin->GetSchema() )
					{
						Result->IconColor = Schema->GetPinTypeColor(Pin->PinType);
					}
					Result->SlateIcon = FSlateIcon("EditorStyle", "Graph.Pin.Disconnected_VarA");
				}
			}
		}
	}
}

void FHardReferenceViewerSearchData::SearchMemberVariables(TMap<FName, FHRVTreeViewItemPtr>& OutPackageMap,	const FAssetRegistryModule& AssetRegistryModule, UBlueprint* Blueprint)
{
	TSet<FName> CurrentVars;
	FBlueprintEditorUtils::GetClassVariableList(Blueprint, CurrentVars);
	for(const FName& VarName : CurrentVars)
	{
		UBlueprint* SourceBlueprint = nullptr;
		int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndexAndBlueprint(Blueprint, VarName, SourceBlueprint);
		if (VarIndex != INDEX_NONE)
		{
			FBPVariableDescription Description = SourceBlueprint->NewVariables[VarIndex];
			UClass* GeneratedClass = SourceBlueprint->GeneratedClass;
			UObject* GeneratedCDO = GeneratedClass->GetDefaultObject();
			FProperty* TargetProperty = FindFProperty<FProperty>(GeneratedClass, Description.VarName);

			if(TargetProperty)
			{
				TArray<const FStructProperty*> EncounteredStructProps;
				EPropertyObjectReferenceType ReferenceType = EPropertyObjectReferenceType::Strong;
				bool bHasStrongReferences = TargetProperty->ContainsObjectReference(EncounteredStructProps, ReferenceType);
				if(bHasStrongReferences)
				{
					void* TargetPropertyAddress = TargetProperty->ContainerPtrToValuePtr<void>(GeneratedCDO);
					FSlateIcon ResultIcon;

					struct PropertyAndAddressTuple
					{
						FProperty* Property = nullptr;
						void* ValueAddress = nullptr;
					};
					TArray<PropertyAndAddressTuple> PropertiesToExamine;

					if(const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(TargetProperty))
					{
						ResultIcon = FSlateIcon("EditorStyle", "Kismet.VariableList.ArrayTypeIcon");
						FScriptArrayHelper ArrayHelper(ArrayProperty, TargetPropertyAddress);
						for(int i=0; i<ArrayHelper.Num(); ++i)
						{
							PropertiesToExamine.Add({ArrayProperty->Inner, ArrayHelper.GetRawPtr(i)});
						}
					}
					else if(const FSetProperty* SetProperty = CastField<FSetProperty>(TargetProperty))
					{
						ResultIcon = FSlateIcon("EditorStyle", "Kismet.VariableList.SetTypeIcon");
						FScriptSetHelper SetHelper(SetProperty, TargetPropertyAddress);
						for(int i=0; i<SetHelper.Num(); ++i)
						{
							PropertiesToExamine.Add({SetHelper.GetElementProperty(), SetHelper.GetElementPtr(i)});
						}
					}
					else if(const FMapProperty* MapProperty = CastField<FMapProperty>(TargetProperty))
					{
						ResultIcon = FSlateIcon("EditorStyle", "Kismet.VariableList.MapValueTypeIcon");
						FScriptMapHelper MapHelper(MapProperty, TargetPropertyAddress);
						for(int i=0; i<MapHelper.Num(); ++i)
						{
							PropertiesToExamine.Add({MapHelper.GetKeyProperty(), MapHelper.GetKeyPtr(i)});
							PropertiesToExamine.Add({MapHelper.GetValueProperty(), MapHelper.GetValuePtr(i)});
						}
					}
					else
					{
						ResultIcon = FSlateIcon("EditorStyle", "Kismet.VariableList.TypeIcon");
						PropertiesToExamine.Add({TargetProperty, TargetPropertyAddress});
					}

					for (const PropertyAndAddressTuple& Tuple : PropertiesToExamine)
					{
						if( FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Tuple.Property) )
						{
							UObject* Object = ObjectProperty->GetObjectPropertyValue(Tuple.ValueAddress);
							if(Object)
							{
								const UPackage* Package = Object->GetPackage();
								if( const FHRVTreeViewItemPtr Result = CheckAddPackageResult(OutPackageMap, AssetRegistryModule, Package) )
								{
									if( UEdGraphSchema_K2 const* Schema = GetDefault<UEdGraphSchema_K2>() )
									{
										Result->IconColor = Schema->GetPinTypeColor(Description.VarType);
										Result->SlateIcon = ResultIcon;
									}
									Result->Name = FText::Format(LOCTEXT("MemberVariable","{0} (Variable)"), FText::FromName(Description.VarName));	
								}
							}
						}
					}
				}
			}
		}
	}
}

FHRVTreeViewItemPtr FHardReferenceViewerSearchData::CheckAddPackageResult(TMap<FName, FHRVTreeViewItemPtr>& OutPackageMap, const FAssetRegistryModule& AssetRegistryModule, const UPackage* Package) const
{
	if( Package )
	{
		const FName PackageName = Package->GetFName();
		if(const FHRVTreeViewItemPtr* FoundHeader = OutPackageMap.Find(PackageName))
		{
			const FHRVTreeViewItemPtr Header = *FoundHeader;
								
			FAssetPackageData AssetPackageData;
			const bool bExists = TryGetAssetPackageData(PackageName, AssetPackageData);
			if(ensure(bExists))
			{
				FHRVTreeViewItemPtr Link = MakeShared<FHRVTreeViewItem>();
				Header->Children.Add(Link);
				return Link; 
			}
		}
	}

	return  nullptr;
}

void FHardReferenceViewerSearchData::GetAssetForPackages(const TArray<FName>& PackageNames, TMap<FName, FAssetData>& OutPackageToAssetData) const
{
#if ENGINE_MAJOR_VERSION < 5
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter Filter;
	for ( auto PackageIt = PackageNames.CreateConstIterator(); PackageIt; ++PackageIt )
	{
		const FString& PackageName = (*PackageIt).ToString();
		Filter.PackageNames.Add(*PackageIt);
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);
	for ( auto AssetIt = AssetDataList.CreateConstIterator(); AssetIt; ++AssetIt )
	{
		OutPackageToAssetData.Add((*AssetIt).PackageName, *AssetIt);
	}
#else
	UE::AssetRegistry::GetAssetForPackages(PackageNames, OutPackageToAssetData);
#endif
}

bool FHardReferenceViewerSearchData::TryGetAssetPackageData(FName PathName, FAssetPackageData& OutPackageData) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

#if ENGINE_MAJOR_VERSION < 5
	if(const FAssetPackageData* pAssetPackageData = AssetRegistryModule.Get().GetAssetPackageData(PathName))
	{
		OutPackageData = *pAssetPackageData;
		return true;
	}
#else
	const UE::AssetRegistry::EExists Result = AssetRegistryModule.TryGetAssetPackageData(PathName, OutPackageData);
	if(Result == UE::AssetRegistry::EExists::Exists)
	{
		return true;
	}
#endif
	return false;
}

FString FHardReferenceViewerSearchData::GetAssetTypeName(const FAssetData& AssetData) const
{
#if ENGINE_MAJOR_VERSION < 5
	return AssetData.AssetClass.ToString();
#else
	return AssetData.AssetClassPath.GetAssetName().ToString();
#endif
}

FAssetData FHardReferenceViewerSearchData::GetAssetDataForObject(const UObject* Object) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FString ObjectPath = Object->GetPathName();

#if ENGINE_MAJOR_VERSION < 5
	return AssetRegistryModule.Get().GetAssetByObjectPath(*ObjectPath);
#else
	return AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
#endif
}

#undef LOCTEXT_NAMESPACE
