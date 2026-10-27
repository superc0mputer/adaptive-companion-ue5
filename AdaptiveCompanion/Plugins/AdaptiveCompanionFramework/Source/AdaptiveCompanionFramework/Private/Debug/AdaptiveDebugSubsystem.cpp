#include "Debug/AdaptiveDebugSubsystem.h"
#include "AdaptiveCompanionFramework.h"
#include "CanvasItem.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DECLARE_CYCLE_STAT(TEXT("Debug Overlay Draw"), STAT_AdaptiveDebugOverlayDraw, STATGROUP_AdaptiveCompanion);

static TAutoConsoleVariable<bool> CVarAdaptiveDebug(
	TEXT("ac.Debug"),
	false,
	TEXT("Shows the Adaptive Companion debug overlay with the pipeline's live values (0 = off, 1 = on)"));

namespace
{
	const TCHAR* const SectionTitles[] = {
		TEXT("1 Telemetry"),
		TEXT("2 Features"),
		TEXT("3 Assistance Need"),
		TEXT("4 Context"),
		TEXT("5 Intervention Selection"),
		TEXT("6 Behavior"),
	};
	static_assert(UE_ARRAY_COUNT(SectionTitles) == static_cast<int32>(EAdaptiveDebugSection::Count), "One title per section");

	// layout in pixels at 1080p, scaled with the viewport height
	constexpr float PanelWidth = 380.f;
	constexpr float Margin = 16.f;
	constexpr float TopOffset = 80.f;
	constexpr float Padding = 8.f;
	constexpr float NameWidth = 170.f;
	constexpr float NameGap = 12.f;
	constexpr float ValueWidth = 44.f;
	constexpr float SectionGap = 6.f;

	const FLinearColor BackgroundColor(0.f, 0.f, 0.f, 0.6f);
	const FLinearColor BarBackgroundColor(0.15f, 0.15f, 0.15f, 0.9f);
	const FColor TitleColor(255, 255, 255);
	const FColor HeaderColor(110, 190, 255);
	const FColor NameColor(220, 220, 220);
	const FColor EmptyColor(130, 130, 130);

	/** Green at 0, yellow at 0.5, red at 1 */
	FLinearColor GetBarColor(double Value)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(Value), 0.f, 1.f);
		const FLinearColor Green(0.2f, 0.8f, 0.3f);
		const FLinearColor Yellow(0.95f, 0.8f, 0.2f);
		const FLinearColor Red(0.95f, 0.25f, 0.2f);
		return Alpha < 0.5f ? FMath::Lerp(Green, Yellow, Alpha * 2.f) : FMath::Lerp(Yellow, Red, (Alpha - 0.5f) * 2.f);
	}

	FString FormatNumber(double Value)
	{
		return FMath::IsFinite(Value) ? FString::Printf(TEXT("%.2f"), Value) : LexToString(Value);
	}

	void DrawRect(UCanvas* Canvas, float X, float Y, float Width, float Height, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(Width, Height), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
}

UAdaptiveDebugSubsystem* UAdaptiveDebugSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UAdaptiveDebugSubsystem>() : nullptr;
}

void UAdaptiveDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// "Game" is set in every game viewport, so the overlay draws in PIE and in builds
	DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UAdaptiveDebugSubsystem::DrawOverlay));
}

void UAdaptiveDebugSubsystem::Deinitialize()
{
	SetDemoRunning(false);

	UDebugDrawService::Unregister(DrawHandle);
	DrawHandle.Reset();

	Super::Deinitialize();
}

void UAdaptiveDebugSubsystem::SetBar(EAdaptiveDebugSection Section, const FString& Name, double Value)
{
	Data.SetBar(Section, Name, Value);
}

void UAdaptiveDebugSubsystem::SetNumber(EAdaptiveDebugSection Section, const FString& Name, double Value)
{
	Data.SetNumber(Section, Name, Value);
}

void UAdaptiveDebugSubsystem::SetText(EAdaptiveDebugSection Section, const FString& Name, const FString& Text)
{
	Data.SetText(Section, Name, Text);
}

void UAdaptiveDebugSubsystem::RemoveValue(EAdaptiveDebugSection Section, const FString& Name)
{
	Data.Remove(Section, Name);
}

void UAdaptiveDebugSubsystem::ClearSection(EAdaptiveDebugSection Section)
{
	Data.ClearSection(Section);
}

void UAdaptiveDebugSubsystem::ClearAll()
{
	Data.ClearAll();
}

bool UAdaptiveDebugSubsystem::IsOverlayVisible()
{
	return CVarAdaptiveDebug.GetValueOnGameThread();
}

void UAdaptiveDebugSubsystem::SetOverlayVisible(bool bVisible)
{
	// same priority as the console, so code and console can both turn it on and off
	CVarAdaptiveDebug->Set(bVisible, ECVF_SetByConsole);
}

bool UAdaptiveDebugSubsystem::ParseSection(const FString& Text, EAdaptiveDebugSection& OutSection)
{
	const int32 Count = static_cast<int32>(EAdaptiveDebugSection::Count);

	if (Text.IsNumeric())
	{
		const int32 Number = FCString::Atoi(*Text);
		if (Number >= 1 && Number <= Count)
		{
			OutSection = static_cast<EAdaptiveDebugSection>(Number - 1);
			return true;
		}
		return false;
	}

	const UEnum* Enum = StaticEnum<EAdaptiveDebugSection>();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Enum->GetNameStringByValue(Index).Equals(Text, ESearchCase::IgnoreCase))
		{
			OutSection = static_cast<EAdaptiveDebugSection>(Index);
			return true;
		}
	}
	return false;
}

const TCHAR* UAdaptiveDebugSubsystem::GetSectionTitle(EAdaptiveDebugSection Section)
{
	return Section < EAdaptiveDebugSection::Count ? SectionTitles[static_cast<int32>(Section)] : TEXT("");
}

void UAdaptiveDebugSubsystem::DrawOverlay(UCanvas* Canvas, APlayerController* PlayerController)
{
	if (!CVarAdaptiveDebug.GetValueOnGameThread() || !Canvas || !PlayerController)
	{
		return;
	}

	// the draw service is global: only draw into this game instance's viewport, once (split screen)
	if (PlayerController->GetGameInstance() != GetGameInstance() || PlayerController->GetLocalPlayer() != GetGameInstance()->GetFirstGamePlayer())
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_AdaptiveDebugOverlayDraw);
	TRACE_CPUPROFILER_EVENT_SCOPE(UAdaptiveDebugSubsystem::DrawOverlay);

	const UFont* Font = GEngine->GetSmallFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 1080.f, 1.f, 4.f);
	const float TextHeight = Font->GetMaxCharHeight() * Scale;
	const float LineHeight = TextHeight + 6.f * Scale;

	const int32 SectionCount = static_cast<int32>(EAdaptiveDebugSection::Count);
	int32 Rows = 1;
	float NameColumn = NameWidth * Scale;
	float TextColumn = 0.f;
	for (int32 Index = 0; Index < SectionCount; ++Index)
	{
		const TArray<FAdaptiveDebugEntry>& Entries = Data.GetEntries(static_cast<EAdaptiveDebugSection>(Index));

		// header + entries, or one "no data" row
		Rows += 1 + FMath::Max(1, Entries.Num());

		// the game chooses the names, so widen the columns (and the panel) for long names and texts
		for (const FAdaptiveDebugEntry& Entry : Entries)
		{
			float TextWidth = 0.f;
			float Unused = 0.f;
			Canvas->TextSize(Font, Entry.Name, TextWidth, Unused, Scale, Scale);
			NameColumn = FMath::Max(NameColumn, TextWidth + NameGap * Scale);

			if (Entry.Kind == EAdaptiveDebugValueKind::Text)
			{
				Canvas->TextSize(Font, Entry.Text, TextWidth, Unused, Scale, Scale);
				TextColumn = FMath::Max(TextColumn, TextWidth);
			}
		}
	}

	const float Pad = Padding * Scale;
	const float Width = FMath::Max(PanelWidth * Scale + NameColumn - NameWidth * Scale, NameColumn + TextColumn + 2.f * Pad);
	const float Height = Rows * LineHeight + SectionCount * SectionGap * Scale + 2.f * Pad;
	const float Left = Margin * Scale;
	const float Top = FMath::Min(TopOffset * Scale, FMath::Max(0.f, Canvas->ClipY - Height));

	DrawRect(Canvas, Left, Top, Width, Height, BackgroundColor);

	const float TextLeft = Left + Pad;
	const float BarLeft = TextLeft + NameColumn;
	const float ValueRight = Left + Width - Pad;
	const float BarWidth = ValueRight - ValueWidth * Scale - BarLeft;
	const float TextOffset = (LineHeight - TextHeight) * 0.5f;

	float Y = Top + Pad;

	auto DrawTextAt = [&](const FString& Text, float X, const FColor& Color)
	{
		Canvas->SetDrawColor(Color);
		Canvas->DrawText(Font, Text, X, Y + TextOffset, Scale, Scale);
	};

	auto DrawTextRight = [&](const FString& Text, float Right, const FColor& Color)
	{
		float TextWidth = 0.f;
		float Unused = 0.f;
		Canvas->TextSize(Font, Text, TextWidth, Unused, Scale, Scale);
		DrawTextAt(Text, Right - TextWidth, Color);
	};

	DrawTextAt(TEXT("Adaptive Companion"), TextLeft, TitleColor);
	DrawTextRight(TEXT("ac.Debug 0 to hide"), ValueRight, EmptyColor);
	Y += LineHeight;

	for (int32 Index = 0; Index < SectionCount; ++Index)
	{
		const EAdaptiveDebugSection Section = static_cast<EAdaptiveDebugSection>(Index);
		const TArray<FAdaptiveDebugEntry>& Entries = Data.GetEntries(Section);

		Y += SectionGap * Scale;
		DrawTextAt(GetSectionTitle(Section), TextLeft, HeaderColor);
		Y += LineHeight;

		if (Entries.IsEmpty())
		{
			DrawTextAt(TEXT("no data"), TextLeft, EmptyColor);
			Y += LineHeight;
			continue;
		}

		for (const FAdaptiveDebugEntry& Entry : Entries)
		{
			DrawTextAt(Entry.Name, TextLeft, NameColor);

			switch (Entry.Kind)
			{
			case EAdaptiveDebugValueKind::Bar:
			{
				const float BarHeight = LineHeight * 0.6f;
				const float BarTop = Y + (LineHeight - BarHeight) * 0.5f;
				DrawRect(Canvas, BarLeft, BarTop, BarWidth, BarHeight, BarBackgroundColor);
				if (FMath::IsFinite(Entry.Value))
				{
					DrawRect(Canvas, BarLeft, BarTop, BarWidth * static_cast<float>(Entry.Value), BarHeight, GetBarColor(Entry.Value));
				}
				DrawTextRight(FormatNumber(Entry.Value), ValueRight, TitleColor);
				break;
			}

			case EAdaptiveDebugValueKind::Number:
				DrawTextAt(FormatNumber(Entry.Value), BarLeft, TitleColor);
				break;

			case EAdaptiveDebugValueKind::Text:
				DrawTextAt(Entry.Text, BarLeft, TitleColor);
				break;
			}

			Y += LineHeight;
		}
	}
}

void UAdaptiveDebugSubsystem::SetDemoRunning(bool bRunning)
{
	if (bRunning == IsDemoRunning())
	{
		return;
	}

	if (bRunning)
	{
		DemoStartSeconds = FPlatformTime::Seconds();
		DemoTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAdaptiveDebugSubsystem::DemoTick));
	}
	else
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DemoTickerHandle);
		DemoTickerHandle.Reset();
		Data.ClearAll();
	}
}

bool UAdaptiveDebugSubsystem::DemoTick(float DeltaTime)
{
	const double T = FPlatformTime::Seconds() - DemoStartSeconds;
	auto Wave = [T](double Period, double Phase) { return 0.5 + 0.5 * FMath::Sin(UE_TWO_PI * T / Period + Phase); };

	using enum EAdaptiveDebugSection;

	SetNumber(Telemetry, TEXT("Combat.DamageTaken"), FMath::RoundToDouble(Wave(7.0, 0.0) * 40.0));
	SetNumber(Telemetry, TEXT("Player.Speed"), Wave(5.0, 1.0) * 600.0);
	SetNumber(Telemetry, TEXT("Camera.YawRate"), Wave(3.0, 2.0) * 180.0);

	SetBar(Features, TEXT("Combat.DamageRate"), Wave(7.0, -0.5));
	SetBar(Features, TEXT("Nav.Backtracking"), Wave(11.0, 0.0));
	SetBar(Features, TEXT("Camera.Spinning"), Wave(3.0, 2.0));

	const double CombatNeed = Wave(9.0, 0.0);
	SetBar(Need, TEXT("Need.Combat"), CombatNeed);
	SetBar(Need, TEXT("Need.Navigation"), Wave(13.0, 1.5));
	SetBar(Need, TEXT("Need.Threat"), Wave(9.0, 0.6));
	SetBar(Need, TEXT("Need.Disorientation"), Wave(6.0, 3.0));
	SetText(Need, TEXT("Level"), CombatNeed > 0.75 ? TEXT("High") : CombatNeed > 0.45 ? TEXT("Elevated") : TEXT("Normal"));

	SetText(Context, TEXT("Scenario"), TEXT("Demo"));
	SetNumber(Context, TEXT("EnemiesNearby"), FMath::RoundToDouble(Wave(9.0, 0.0) * 4.0));

	SetBar(Selection, TEXT("DrawAggro"), CombatNeed * 0.9);
	SetBar(Selection, TEXT("NavHint"), Wave(13.0, 1.5) * 0.8);
	SetText(Selection, TEXT("Selected"), CombatNeed > 0.75 ? TEXT("DrawAggro") : TEXT("none"));

	SetText(Behavior, TEXT("Companion"), CombatNeed > 0.75 ? TEXT("Intervention") : TEXT("Default/Follow"));

	return true;
}

// Console commands

static FAutoConsoleCommandWithWorldAndArgs GAdaptiveDebugSetCommand(
	TEXT("ac.Debug.Set"),
	TEXT("Sets a test value in the debug overlay: ac.Debug.Set <Section> <Name> <Value>. Section is a name (Telemetry, Features, Need, Context, ")
	TEXT("Selection, Behavior) or 1-6. Numbers in [0, 1] are shown as bars, other numbers as numbers, everything else as text"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UAdaptiveDebugSubsystem* Debug = UAdaptiveDebugSubsystem::Get(World);
		if (!Debug)
		{
			return;
		}

		EAdaptiveDebugSection Section;
		if (Args.Num() < 3 || !UAdaptiveDebugSubsystem::ParseSection(Args[0], Section))
		{
			UE_LOG(LogAdaptiveCompanionFramework, Warning, TEXT("Usage: ac.Debug.Set <Section> <Name> <Value>"));
			return;
		}

		const FString Value = FString::Join(TArrayView<const FString>(Args).RightChop(2), TEXT(" "));
		double Number = 0.0;
		if (LexTryParseString(Number, *Value))
		{
			if (Number >= 0.0 && Number <= 1.0)
			{
				Debug->SetBar(Section, Args[1], Number);
			}
			else
			{
				Debug->SetNumber(Section, Args[1], Number);
			}
		}
		else
		{
			Debug->SetText(Section, Args[1], Value);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GAdaptiveDebugClearCommand(
	TEXT("ac.Debug.Clear"),
	TEXT("Clears all values of the debug overlay, or of one section: ac.Debug.Clear [Section]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UAdaptiveDebugSubsystem* Debug = UAdaptiveDebugSubsystem::Get(World);
		if (!Debug)
		{
			return;
		}

		EAdaptiveDebugSection Section;
		if (Args.IsEmpty())
		{
			Debug->ClearAll();
		}
		else if (UAdaptiveDebugSubsystem::ParseSection(Args[0], Section))
		{
			Debug->ClearSection(Section);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GAdaptiveDebugDemoCommand(
	TEXT("ac.Debug.Demo"),
	TEXT("Toggles animated test values in every section of the debug overlay and shows the overlay"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UAdaptiveDebugSubsystem* Debug = UAdaptiveDebugSubsystem::Get(World))
		{
			Debug->SetDemoRunning(!Debug->IsDemoRunning());
			if (Debug->IsDemoRunning())
			{
				UAdaptiveDebugSubsystem::SetOverlayVisible(true);
			}
		}
	}));
