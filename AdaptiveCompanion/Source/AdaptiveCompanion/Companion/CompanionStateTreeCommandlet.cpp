#include "CompanionStateTreeCommandlet.h"
#include "AdaptiveCompanion.h"

#if WITH_EDITOR
#include "CompanionCharacter.h"
#include "CompanionSettings.h"
#include "CompanionStateTreeTasks.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "Pipeline/AdaptiveCompanionAction.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

namespace
{
	const FName UnknownActionState = TEXT("UnknownAction");

	UStateTreeState* FindState(const UStateTreeEditorData& EditorData, FName Name)
	{
		UStateTreeState* Found = nullptr;
		EditorData.VisitHierarchy([&Found, Name](UStateTreeState& State, UStateTreeState* Parent)
		{
			if (State.Name == Name)
			{
				Found = &State;
				return EStateTreeVisitor::Break;
			}
			return EStateTreeVisitor::Continue;
		});
		return Found;
	}

	bool HasTransition(const UStateTreeState& State, EStateTreeTransitionTrigger Trigger)
	{
		return State.Transitions.ContainsByPredicate([Trigger](const FStateTreeTransition& Transition) { return Transition.Trigger == Trigger; });
	}

	/** "Action.NavigationHint" → "NavigationHint" */
	FName StateNameFor(FName Action)
	{
		FString Name = Action.ToString();
		Name.RemoveFromStart(TEXT("Action."));
		return FName(*Name);
	}
}
#endif

UCompanionStateTreeCommandlet::UCompanionStateTreeCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UCompanionStateTreeCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	const TSubclassOf<ACompanionCharacter> CompanionClass = GetDefault<UCompanionSettings>()->CompanionClass.LoadSynchronous();
	const ACompanionCharacter* Companion = CompanionClass ? CompanionClass->GetDefaultObject<ACompanionCharacter>() : nullptr;
	UStateTree* StateTree = Companion ? Companion->CompanionStateTree.Get() : nullptr;
	UStateTreeEditorData* EditorData = StateTree ? Cast<UStateTreeEditorData>(StateTree->EditorData) : nullptr;
	if (!EditorData)
	{
		UE_LOG(LogAdaptiveCompanion, Error, TEXT("CompanionStateTree: no companion class with a StateTree in Project Settings > Game > Companion."));
		return 1;
	}

	UStateTreeState* Intervention = FindState(*EditorData, TEXT("Intervention"));
	UStateTreeState* Default = FindState(*EditorData, TEXT("Default"));
	if (!Intervention || !Default)
	{
		UE_LOG(LogAdaptiveCompanion, Error, TEXT("CompanionStateTree: %s has no Intervention or Default state."), *StateTree->GetPathName());
		return 1;
	}

	StateTree->Modify();
	EditorData->Modify();
	Intervention->Modify();

	// the children carry out the actions; tasks on the parent would run alongside every one of them
	if (!Intervention->Tasks.IsEmpty())
	{
		UE_LOG(LogAdaptiveCompanion, Display, TEXT("CompanionStateTree: removed %d task(s) from Intervention, its children carry out the actions now."), Intervention->Tasks.Num());
		Intervention->Tasks.Reset();
	}
	if (!HasTransition(*Intervention, EStateTreeTransitionTrigger::OnStateCompleted))
	{
		Intervention->AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, Default);
	}
	if (!HasTransition(*Intervention, EStateTreeTransitionTrigger::OnStateFailed))
	{
		Intervention->AddTransition(EStateTreeTransitionTrigger::OnStateFailed, EStateTreeTransitionType::GotoState, Default);
	}

	// the fallback has to stay the last child, since children are tried in order
	TObjectPtr<UStateTreeState> Unknown;
	if (const int32 Index = Intervention->Children.IndexOfByPredicate([](const UStateTreeState* State) { return State && State->Name == UnknownActionState; }); Index != INDEX_NONE)
	{
		Unknown = Intervention->Children[Index];
		Intervention->Children.RemoveAt(Index);
	}

	int32 Added = 0;
	for (const UAdaptiveCompanionAction* Action : Companion->Actions)
	{
		if (!Action || Action->ActionName.IsNone())
		{
			continue;
		}

		const FName StateName = StateNameFor(Action->ActionName);
		if (Intervention->Children.ContainsByPredicate([StateName](const UStateTreeState* State) { return State && State->Name == StateName; }))
		{
			continue;
		}

		UStateTreeState& State = Intervention->AddChildState(StateName);
		State.Description = FString::Printf(TEXT("Carries out %s. Placeholder until its behavior exists."), *Action->ActionName.ToString());
		State.AddEnterCondition<FCompanionIsCurrentInterventionCondition>().GetInstanceData().Intervention = Action->ActionName;
		State.AddTask<FCompanionInterventionPlaceholderTask>();
		++Added;
		UE_LOG(LogAdaptiveCompanion, Display, TEXT("CompanionStateTree: added state %s for %s."), *StateName.ToString(), *Action->ActionName.ToString());
	}

	if (Unknown)
	{
		Intervention->Children.Add(Unknown);
	}
	else
	{
		UStateTreeState& State = Intervention->AddChildState(UnknownActionState);
		State.Description = TEXT("Catches actions without a state of their own and reports them as failed.");
		State.AddTask<FCompanionReportFailedTask>();
		++Added;
		UE_LOG(LogAdaptiveCompanion, Display, TEXT("CompanionStateTree: added state %s."), *UnknownActionState.ToString());
	}

	EditorData->ReparentStates();

	FStateTreeCompilerLog CompilerLog;
	if (!UStateTreeEditingSubsystem::CompileStateTree(StateTree, CompilerLog))
	{
		CompilerLog.DumpToLog(LogAdaptiveCompanion);
		UE_LOG(LogAdaptiveCompanion, Error, TEXT("CompanionStateTree: %s doesn't compile, not saved."), *StateTree->GetPathName());
		return 1;
	}

	UPackage* Package = StateTree->GetPackage();
	Package->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, StateTree, *Filename, SaveArgs))
	{
		UE_LOG(LogAdaptiveCompanion, Error, TEXT("CompanionStateTree: couldn't save %s."), *Filename);
		return 1;
	}

	UE_LOG(LogAdaptiveCompanion, Display, TEXT("CompanionStateTree: %d state(s) added, %s compiled and saved."), Added, *StateTree->GetPathName());
	return 0;
#else
	return 0;
#endif
}
