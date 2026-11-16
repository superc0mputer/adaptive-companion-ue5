#include "CompanionThreatIndicator.h"
#include "CameraInputTelemetrySubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SCompanionThreatIndicator::Construct(const FArguments& InArgs)
{
	PlayerController = InArgs._PlayerController;
	Threat = InArgs._Threat;
	StartTime = FSlateApplication::Get().GetCurrentTime();
	SetVisibility(EVisibility::HitTestInvisible);
}

FString SCompanionThreatIndicator::DirectionText(float RelativeYaw)
{
	if (FMath::Abs(RelativeYaw) > 135.0f)
	{
		return TEXT("Behind you!");
	}
	return RelativeYaw < 0.0f ? TEXT("Left!") : TEXT("Right!");
}

bool SCompanionThreatIndicator::GetRelativeYaw(const APlayerController& Controller, const AActor& InThreat, float& OutYaw)
{
	const APlayerCameraManager* Camera = Controller.PlayerCameraManager.Get();
	if (!Camera)
	{
		return false;
	}

	const FVector ToThreat = InThreat.GetActorLocation() - Camera->GetCameraLocation();
	OutYaw = FRotator::NormalizeAxis(ToThreat.Rotation().Yaw - Camera->GetCameraRotation().Yaw);
	return true;
}

int32 SCompanionThreatIndicator::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const APlayerController* Controller = PlayerController.Get();
	const AActor* Target = Threat.Get();
	float Yaw = 0.0f;
	if (!Controller || !IsValid(Target) || !GetRelativeYaw(*Controller, *Target, Yaw)
		|| UCameraInputTelemetrySubsystem::IsInCameraView(*Controller, Target->GetActorLocation()))
	{
		return LayerId;
	}

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FVector2D Center = Size * 0.5;

	// forward is up: the arrow sits just inside the screen edge, in the threat's direction. Scaled
	// to the screen, so it is equally prominent in every window size
	const double Scale = FMath::Min(Size.X, Size.Y) / 900.0;
	const double Radians = FMath::DegreesToRadians(Yaw);
	const FVector2D Direction(FMath::Sin(Radians), -FMath::Cos(Radians));
	// the furthest point in Direction that stays Margin inside the screen
	const double Margin = 30.0 * Scale;
	const double ToEdge = FMath::Min(
		FMath::Abs(Direction.X) > KINDA_SMALL_NUMBER ? (Size.X * 0.5 - Margin) / FMath::Abs(Direction.X) : UE_BIG_NUMBER,
		FMath::Abs(Direction.Y) > KINDA_SMALL_NUMBER ? (Size.Y * 0.5 - Margin) / FMath::Abs(Direction.Y) : UE_BIG_NUMBER);
	const FVector2D Tip = Center + Direction * ToEdge;

	// pulses twice a second, so it catches the eye at the edge of the screen
	const double Time = FSlateApplication::Get().GetCurrentTime() - StartTime;
	const float Alpha = 0.7f + 0.3f * FMath::Cos(Time * UE_TWO_PI * 2.0);
	const FLinearColor Red(1.0f, 0.12f, 0.05f, Alpha);
	const FLinearColor Shade(0.0f, 0.0f, 0.0f, 0.55f * Alpha);

	// two chevrons pointing outwards, each drawn over a dark outline so it also reads on bright sky and walls
	const FVector2D Side(-Direction.Y, Direction.X);
	const double Length = 55.0 * Scale;
	const double Width = 60.0 * Scale;
	for (int32 i = 0; i < 2; ++i)
	{
		const FVector2D ChevronTip = Tip - Direction * (i * 40.0 * Scale);
		const TArray<FVector2D> Chevron = { ChevronTip - Direction * Length + Side * Width, ChevronTip, ChevronTip - Direction * Length - Side * Width };
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Chevron, ESlateDrawEffect::None, Shade, true, 24.0f * Scale);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Chevron, ESlateDrawEffect::None, Red, true, 15.0f * Scale);
	}

	// the words on a dark box further inside the screen, centered on the arrow's direction
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", FMath::RoundToInt(30.0 * Scale));
	const FString Text = DirectionText(Yaw);
	const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
	const FVector2D Padding(14.0 * Scale, 6.0 * Scale);
	const FVector2D BoxSize = TextSize + Padding * 2.0;
	const double BoxExtent = 0.5 * (FMath::Abs(Direction.X) * BoxSize.X + FMath::Abs(Direction.Y) * BoxSize.Y);
	const FVector2D BoxCenter = Tip - Direction * (Length + 40.0 * Scale + 20.0 * Scale + BoxExtent);
	const FVector2D BoxOrigin = BoxCenter - BoxSize * 0.5;
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(BoxOrigin)), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Shade);
	FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(BoxOrigin + Padding)), Text, Font, ESlateDrawEffect::None, Red);

	return LayerId + 1;
}
