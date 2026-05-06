// Copyright Untry. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateBrush.h"
#include "Styling/CoreStyle.h"

/**
 * Brand palette + typography for the plugin UI.
 * Colors are inspired by Anthropic's own brand: warm terracotta accent,
 * cream/paper tones for user voice, neutral dark for assistant,
 * muted purples for thinking. Readable on standard dark editor themes.
 *
 * All colors are in linear space — FLinearColor values are gamma-decoded
 * when assigned via FSlateColor, so what you see matches a sRGB #hex swatch.
 */
namespace ClaudeAgentStyle
{
	// -------------------------------------------------------------------
	// Signature brand colors
	// -------------------------------------------------------------------

	/** Anthropic terracotta — the signature accent. Approximately #CC785C. */
	inline FLinearColor BrandAccent()         { return FLinearColor::FromSRGBColor(FColor(204, 120,  92)); }
	/** Darker hover state for the accent. */
	inline FLinearColor BrandAccentDark()     { return FLinearColor::FromSRGBColor(FColor(170,  90,  68)); }
	/** Muted cream — paper tone. */
	inline FLinearColor BrandCream()          { return FLinearColor::FromSRGBColor(FColor(240, 238, 229)); }

	// -------------------------------------------------------------------
	// Canvas / containers
	// -------------------------------------------------------------------

	/** Overall panel background — very dark neutral, slightly warmer than pure black. */
	inline FLinearColor CanvasBg()            { return FLinearColor::FromSRGBColor(FColor( 22,  21,  20)); }
	/** Slightly raised surface — sidebars, groupings. */
	inline FLinearColor SurfaceBg()           { return FLinearColor::FromSRGBColor(FColor( 30,  28,  26)); }
	/** Hovered/active item in sidebar. */
	inline FLinearColor SurfaceHover()        { return FLinearColor::FromSRGBColor(FColor( 46,  42,  38)); }
	/** Soft separator line. */
	inline FLinearColor DividerColor()        { return FLinearColor::FromSRGBColor(FColor( 54,  50,  46)); }

	// -------------------------------------------------------------------
	// Message bubbles — one tone per role
	// -------------------------------------------------------------------

	// User — warm cream / paper
	inline FLinearColor UserBg()              { return FLinearColor::FromSRGBColor(FColor( 42,  38,  34)); }
	inline FLinearColor UserAccent()          { return FLinearColor::FromSRGBColor(FColor(212, 193, 160)); }

	// Claude — the hero color, terracotta
	inline FLinearColor ClaudeBg()            { return FLinearColor::FromSRGBColor(FColor( 38,  33,  31)); }
	inline FLinearColor ClaudeAccent()        { return BrandAccent(); }

	// Thinking — muted warm purple (works with terracotta)
	inline FLinearColor ThinkingBg()          { return FLinearColor::FromSRGBColor(FColor( 36,  31,  40)); }
	inline FLinearColor ThinkingAccent()      { return FLinearColor::FromSRGBColor(FColor(170, 152, 198)); }

	// Tool call — amber/honey, signals "action in flight"
	inline FLinearColor ToolCallBg()          { return FLinearColor::FromSRGBColor(FColor( 43,  36,  25)); }
	inline FLinearColor ToolCallAccent()      { return FLinearColor::FromSRGBColor(FColor(224, 164,  90)); }

	// Tool result ok
	inline FLinearColor ToolOkBg()            { return FLinearColor::FromSRGBColor(FColor( 26,  38,  30)); }
	inline FLinearColor ToolOkAccent()        { return FLinearColor::FromSRGBColor(FColor(132, 184, 130)); }

	// Error
	inline FLinearColor ErrorBg()             { return FLinearColor::FromSRGBColor(FColor( 50,  28,  28)); }
	inline FLinearColor ErrorAccent()         { return FLinearColor::FromSRGBColor(FColor(214, 108,  98)); }

	// -------------------------------------------------------------------
	// Text
	// -------------------------------------------------------------------

	inline FLinearColor TextPrimary()         { return FLinearColor::FromSRGBColor(FColor(230, 224, 214)); }
	inline FLinearColor TextSecondary()       { return FLinearColor::FromSRGBColor(FColor(160, 154, 144)); }
	inline FLinearColor TextMuted()           { return FLinearColor::FromSRGBColor(FColor(112, 108, 102)); }

	// -------------------------------------------------------------------
	// Fonts
	// -------------------------------------------------------------------

	inline FSlateFontInfo HeaderFont()        { return FCoreStyle::GetDefaultFontStyle("Bold",    10); }
	inline FSlateFontInfo BodyFont()          { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }
	inline FSlateFontInfo SmallFont()         { return FCoreStyle::GetDefaultFontStyle("Regular",  8); }
	inline FSlateFontInfo MonoFont()          { return FCoreStyle::GetDefaultFontStyle("Mono",     9); }
	inline FSlateFontInfo TitleFont()         { return FCoreStyle::GetDefaultFontStyle("Bold",    11); }
}
