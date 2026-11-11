// sgtc_sniper_fx.cpp — StargateTC client-side sniper/energy-weapon feedback
// (NEW file): scope vignette, exponentially smoothed zoom and the SgtcHit
// hit-marker/sound layer.
//
// Everything is drawn with the engine's flat primitives (pfnFillRGBABlend /
// pfnFillRGBA) so no sprite or overlay asset is needed. The vignette follows
// the aim sway from sgtc_aim.h, which keeps the scope feeling alive while the
// player holds breath; the hit markers stay nailed to the screen centre.

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "sgtc_aim.h"
#include "sgtc_sniper_fx.h"

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
#define SGTC_SCOPE_SIDE_BANDS	24		// bands down each side edge
#define SGTC_SCOPE_VERT_BANDS	16		// bands across top/bottom edges
#define SGTC_SCOPE_SIDE_FRAC	0.22f	// width covered by the side ramp
#define SGTC_SCOPE_VERT_FRAC	0.16f	// height covered by the top/bottom ramp
#define SGTC_SCOPE_SIDE_ALPHA	150		// outermost side band alpha
#define SGTC_SCOPE_VERT_ALPHA	120		// outermost top/bottom band alpha
#define SGTC_SCOPE_CORNER_ALPHA	70		// extra corner reinforcement
#define SGTC_SCOPE_BEZEL_ALPHA	190		// 1px scope ring

#define SGTC_SCOPE_SWAY_PX		3.0f	// sway degrees -> screen pixels
#define SGTC_SCOPE_SWAY_MAX		14		// px clamp, keep the centre clear

#define SGTC_HITMARK_MAX		3		// active markers kept alive
#define SGTC_HITMARK_LIFE		0.28f	// seconds

// SgtcHit flags (wire byte 0).
#define SGTC_HIT_F_HIT			1
#define SGTC_HIT_F_HEADSHOT		2
#define SGTC_HIT_F_KILL			4

// ---------------------------------------------------------------------------
// Cvars
// ---------------------------------------------------------------------------
static cvar_t *s_pCvarScope = NULL;		// sgtc_sniper_scope, client only
static cvar_t *s_pCvarHitmark = NULL;	// sgtc_sniper_hitmark, client only

// Zoom/hit state is reset here too: hud_msg.cpp clears g_lastFOV/m_inScope on
// a new level, and a stale zoom target would leave the view stuck scoped.
static void SGTC_SniperFx_ResetState( void );

void SGTC_SniperFx_Init( void )
{
	if( s_pCvarScope == NULL )
		s_pCvarScope = CVAR_CREATE( "sgtc_sniper_scope", "1", FCVAR_ARCHIVE );
	if( s_pCvarHitmark == NULL )
		s_pCvarHitmark = CVAR_CREATE( "sgtc_sniper_hitmark", "1", FCVAR_ARCHIVE );

	SGTC_SniperFx_ResetState();
}

// ---------------------------------------------------------------------------
// Zoom easing
// ---------------------------------------------------------------------------
static float s_flFovTarget = 0.0f;	// last SetFOV value (0 = no override)
static float s_flFovCur = 0.0f;		// eased FOV handed to the engine
static bool s_bFovInit = false;

// default_fov is a private CHud member; the cvar itself is registered by
// CHud::Init so the engine lookup is enough here.
static float SGTC_FovDefault( void )
{
	float def = CVAR_GET_FLOAT( "default_fov" );

	if( def < 10.0f || def > 170.0f )
		def = 90.0f;
	return def;
}

void SGTC_SniperFx_SetFovTarget( int newfov )
{
	if( newfov < 0 )
		newfov = 0;

	if( !s_bFovInit )
	{
		// First target of the level: start the ease from the default FOV so
		// scoping in is smooth instead of snapping.
		s_flFovCur = SGTC_FovDefault();
		s_bFovInit = true;
	}

	const bool wasScoped = ( s_flFovTarget > 0.0f );
	const bool scoped = ( newfov > 0 );

	s_flFovTarget = (float)newfov;

	if( wasScoped == scoped )
		return;
}

float SGTC_SniperFx_GetSmoothedFov( float frametime )
{
	const float target = ( s_flFovTarget > 0.5f ) ? s_flFovTarget : SGTC_FovDefault();

	if( !s_bFovInit )
	{
		s_flFovCur = target;
		s_bFovInit = true;
	}

	if( frametime > 0.0f )
		s_flFovCur += ( target - s_flFovCur ) * Q_min( 1.0f, frametime * 14.0f );

	// Settled back on the default FOV: report 0 so the engine (and the stock
	// sensitivity logic in CHud::Think) sees "no zoom override".
	float settle = target - s_flFovCur;
	if( settle < 0.0f )
		settle = -settle;
	if( s_flFovTarget <= 0.5f && settle < 0.25f )
		return 0.0f;

	return s_flFovCur;
}

// ---------------------------------------------------------------------------
// Hit markers
// ---------------------------------------------------------------------------
struct sgtc_hitmark_t
{
	float	time;	// client time the marker was created
	int		flags;	// SGTC_HIT_F_*
	bool	active;
};

static sgtc_hitmark_t s_hitmarks[SGTC_HITMARK_MAX];
static int s_iHitmarkNext = 0;

static void SGTC_Fx_AddHitmark( int flags )
{
	s_hitmarks[s_iHitmarkNext].time = gEngfuncs.GetClientTime();
	s_hitmarks[s_iHitmarkNext].flags = flags;
	s_hitmarks[s_iHitmarkNext].active = true;
	s_iHitmarkNext = ( s_iHitmarkNext + 1 ) % SGTC_HITMARK_MAX;
}

// One diagonal tick: a run of 2x2 blocks stepping away from the centre.
static void SGTC_Fx_DrawTick( int cx, int cy, int dirX, int dirY, int inner, int len, int r, int g, int b, int a )
{
	for( int i = 0; i < len; i++ )
	{
		int x = cx + dirX * ( inner + i ) - ( dirX < 0 ? 1 : 0 );
		int y = cy + dirY * ( inner + i ) - ( dirY < 0 ? 1 : 0 );
		FillRGBA( x, y, 2, 2, r, g, b, a );
	}
}

static void SGTC_Fx_DrawHitmarks( void )
{
	const float now = gEngfuncs.GetClientTime();
	const int cx = ScreenWidth / 2;
	const int cy = ScreenHeight / 2;
	const int scale = Q_max( 1, ScreenHeight / 480 );
	const int inner = 8 * scale;
	const int len = 7 * scale;

	for( int i = 0; i < SGTC_HITMARK_MAX; i++ )
	{
		if( !s_hitmarks[i].active )
			continue;

		const float age = now - s_hitmarks[i].time;
		if( age < 0.0f || age >= SGTC_HITMARK_LIFE )
			continue;

		int a = (int)( 255.0f * ( 1.0f - age / SGTC_HITMARK_LIFE ) );
		if( a < 24 )
			a = 24;

		const int flags = s_hitmarks[i].flags;

		if( flags & SGTC_HIT_F_KILL )
		{
			// Kill: larger gold cross plus the usual ticks.
			const int arm = inner + len + 2 * scale;
			FillRGBA( cx - arm, cy - 1, arm * 2, 3, 255, 200, 40, a );
			FillRGBA( cx - 1, cy - arm, 3, arm * 2, 255, 200, 40, a );
			SGTC_Fx_DrawTick( cx, cy, 1, 1, inner, len + scale, 255, 200, 40, a );
			SGTC_Fx_DrawTick( cx, cy, 1, -1, inner, len + scale, 255, 200, 40, a );
			SGTC_Fx_DrawTick( cx, cy, -1, 1, inner, len + scale, 255, 200, 40, a );
			SGTC_Fx_DrawTick( cx, cy, -1, -1, inner, len + scale, 255, 200, 40, a );
			continue;
		}

		int r = 255, g = 255, b = 255;
		if( flags & SGTC_HIT_F_HEADSHOT )
		{
			r = 255;
			g = 90;
			b = 30;
		}

		SGTC_Fx_DrawTick( cx, cy, 1, 1, inner, len, r, g, b, a );
		SGTC_Fx_DrawTick( cx, cy, 1, -1, inner, len, r, g, b, a );
		SGTC_Fx_DrawTick( cx, cy, -1, 1, inner, len, r, g, b, a );
		SGTC_Fx_DrawTick( cx, cy, -1, -1, inner, len, r, g, b, a );
	}
}

int SGTC_SniperFx_MsgFunc_SgtcHit( const char *pszName, int iSize, void *pbuf )
{
	if( iSize < 2 || pbuf == NULL )
		return 1;	// malformed/truncated: nothing to show, never crash

	BEGIN_READ( pbuf, iSize );

	const int flags = READ_BYTE();
	(void)READ_BYTE();	// damage, byte 1 — markers are icon-only for now

	if( s_pCvarHitmark == NULL )
		s_pCvarHitmark = CVAR_CREATE( "sgtc_sniper_hitmark", "1", FCVAR_ARCHIVE );

	if( s_pCvarHitmark == NULL || s_pCvarHitmark->value == 0.0f )
		return 1;

	if( !( flags & ( SGTC_HIT_F_HIT | SGTC_HIT_F_HEADSHOT | SGTC_HIT_F_KILL ) ) )
		return 1;

	SGTC_Fx_AddHitmark( flags );

	return 1;
}

// ---------------------------------------------------------------------------
// Scope overlay
// ---------------------------------------------------------------------------
// Blended black rectangles are clipped to the screen so a swayed overlay can
// never write outside the framebuffer (or leave an unshaded seam).
static void SGTC_Fx_FillBlendClamped( int x, int y, int w, int h, int alpha )
{
	if( w <= 0 || h <= 0 )
		return;

	if( x < 0 )
	{
		w += x;
		x = 0;
	}
	if( y < 0 )
	{
		h += y;
		y = 0;
	}

	if( x + w > ScreenWidth )
		w = ScreenWidth - x;
	if( y + h > ScreenHeight )
		h = ScreenHeight - y;

	if( w <= 0 || h <= 0 )
		return;

	if( alpha <= 0 )
		return;
	if( alpha > 255 )
		alpha = 255;

	gEngfuncs.pfnFillRGBABlend( x, y, w, h, 0, 0, 0, alpha );
}

// Hold-breath stamina: a thin bar under the reticle while the lung is not
// full, so the exhaust/cooldown cycle is readable without another HUD element.
static void SGTC_Fx_DrawBreathMeter( void )
{
	const float stamina = SGTC_Aim_Stamina01();
	const bool holding = SGTC_Aim_HoldingBreath();

	if( stamina >= 0.999f && !holding )
		return;

	int w = ScreenWidth / 6;
	int h = 4;
	int x = ( ScreenWidth - w ) / 2;
	int y = ScreenHeight - ScreenHeight / 8;

	if( w < 24 || h < 2 )
		return;

	FillRGBA( x - 1, y - 1, w + 2, h + 2, 0, 0, 0, 140 );
	FillRGBA( x, y, w, h, 30, 40, 50, 170 );

	int filled = (int)( (float)w * stamina + 0.5f );
	if( filled > 0 )
	{
		if( holding )
			FillRGBA( x, y, filled, h, 120, 220, 255, 230 );
		else
			FillRGBA( x, y, filled, h, 90, 140, 170, 210 );
	}
}

static void SGTC_Fx_DrawVignette( int shiftX, int shiftY )
{
	const int sw = ScreenWidth;
	const int sh = ScreenHeight;

	// Full-height coverage even when the overlay is shifted up/down.
	const int bandTop = ( shiftY < 0 ) ? shiftY : 0;
	const int bandBottom = sh + ( ( shiftY > 0 ) ? shiftY : 0 );
	const int bandHeight = bandBottom - bandTop;

	int sideSpan = (int)( sw * SGTC_SCOPE_SIDE_FRAC );
	if( sideSpan < SGTC_SCOPE_SIDE_BANDS )
		sideSpan = SGTC_SCOPE_SIDE_BANDS;

	// Left/right: alpha ramps 0 -> SGTC_SCOPE_SIDE_ALPHA going outwards.
	for( int i = 0; i < SGTC_SCOPE_SIDE_BANDS; i++ )
	{
		int x0 = sideSpan * i / SGTC_SCOPE_SIDE_BANDS;
		int x1 = sideSpan * ( i + 1 ) / SGTC_SCOPE_SIDE_BANDS;
		if( x1 <= x0 )
			x1 = x0 + 1;

		const int alpha = SGTC_SCOPE_SIDE_ALPHA * ( SGTC_SCOPE_SIDE_BANDS - i ) / SGTC_SCOPE_SIDE_BANDS;

		int lx0 = x0 + shiftX;
		const int lx1 = x1 + shiftX;
		if( i == 0 && lx0 > 0 )
			lx0 = 0;	// outermost band always reaches the screen edge
		SGTC_Fx_FillBlendClamped( lx0, bandTop, lx1 - lx0, bandHeight, alpha );

		const int rx0 = sw - x1 + shiftX;
		int rx1 = sw - x0 + shiftX;
		if( i == 0 && rx1 < sw )
			rx1 = sw;
		SGTC_Fx_FillBlendClamped( rx0, bandTop, rx1 - rx0, bandHeight, alpha );
	}

	// Top/bottom: full-width coverage even when the overlay is shifted.
	const int bandLeft = ( shiftX < 0 ) ? shiftX : 0;
	const int bandRight = sw + ( ( shiftX > 0 ) ? shiftX : 0 );
	const int bandWidth = bandRight - bandLeft;

	int vertSpan = (int)( sh * SGTC_SCOPE_VERT_FRAC );
	if( vertSpan < SGTC_SCOPE_VERT_BANDS )
		vertSpan = SGTC_SCOPE_VERT_BANDS;

	// alpha ramps 0 -> SGTC_SCOPE_VERT_ALPHA going outwards.
	for( int i = 0; i < SGTC_SCOPE_VERT_BANDS; i++ )
	{
		int y0 = vertSpan * i / SGTC_SCOPE_VERT_BANDS;
		int y1 = vertSpan * ( i + 1 ) / SGTC_SCOPE_VERT_BANDS;
		if( y1 <= y0 )
			y1 = y0 + 1;

		const int alpha = SGTC_SCOPE_VERT_ALPHA * ( SGTC_SCOPE_VERT_BANDS - i ) / SGTC_SCOPE_VERT_BANDS;

		int ty0 = y0 + shiftY;
		const int ty1 = y1 + shiftY;
		if( i == 0 && ty0 > 0 )
			ty0 = 0;
		SGTC_Fx_FillBlendClamped( bandLeft, ty0, bandWidth, ty1 - ty0, alpha );

		const int by0 = sh - y1 + shiftY;
		int by1 = sh - y0 + shiftY;
		if( i == 0 && by1 < sh )
			by1 = sh;
		SGTC_Fx_FillBlendClamped( bandLeft, by0, bandWidth, by1 - by0, alpha );
	}

	// Reinforced corners: the side and edge ramps already stack there, this
	// deepens them further so the sight picture reads as a round tube.
	const int cornerW = sw * 10 / 100;
	const int cornerH = sh * 14 / 100;

	SGTC_Fx_FillBlendClamped( shiftX, shiftY, cornerW, cornerH, SGTC_SCOPE_CORNER_ALPHA );
	SGTC_Fx_FillBlendClamped( sw - cornerW + shiftX, shiftY, cornerW, cornerH, SGTC_SCOPE_CORNER_ALPHA );
	SGTC_Fx_FillBlendClamped( shiftX, sh - cornerH + shiftY, cornerW, cornerH, SGTC_SCOPE_CORNER_ALPHA );
	SGTC_Fx_FillBlendClamped( sw - cornerW + shiftX, sh - cornerH + shiftY, cornerW, cornerH, SGTC_SCOPE_CORNER_ALPHA );

	// 1px bezel ring inset ~4% — four flat lines, no sprite.
	int insetX = sw * 4 / 100;
	int insetY = sh * 4 / 100;
	if( insetX < 2 )
		insetX = 2;
	if( insetY < 2 )
		insetY = 2;

	const int x0 = insetX + shiftX;
	const int x1 = sw - insetX + shiftX - 1;
	const int y0 = insetY + shiftY;
	const int y1 = sh - insetY + shiftY - 1;

	if( x1 > x0 && y1 > y0 )
	{
		FillRGBA( x0, y0, x1 - x0, 1, 0, 0, 0, SGTC_SCOPE_BEZEL_ALPHA );
		FillRGBA( x0, y1, x1 - x0, 1, 0, 0, 0, SGTC_SCOPE_BEZEL_ALPHA );
		FillRGBA( x0, y0, 1, y1 - y0, 0, 0, 0, SGTC_SCOPE_BEZEL_ALPHA );
		FillRGBA( x1, y0, 1, y1 - y0, 0, 0, 0, SGTC_SCOPE_BEZEL_ALPHA );
	}
}

void SGTC_SniperFx_Draw( void )
{
	if( s_pCvarScope == NULL || s_pCvarHitmark == NULL )
		SGTC_SniperFx_Init();

	if( gHUD.m_iHideHUDDisplay & HIDEHUD_ALL )
		return;

	// Scope overlay only while scoped; the sway moves the whole overlay a few
	// pixels so the tube breathes instead of sitting frozen.
	if( gHUD.m_inScope && ( s_pCvarScope == NULL || s_pCvarScope->value != 0.0f )
		&& ScreenWidth >= 320 && ScreenHeight >= 240 )
	{
		float pitchOff = 0.0f, yawOff = 0.0f;
		SGTC_Aim_GetOffset( &pitchOff, &yawOff );

		int shiftX = (int)( yawOff * SGTC_SCOPE_SWAY_PX );
		int shiftY = (int)( pitchOff * SGTC_SCOPE_SWAY_PX );

		if( shiftX > SGTC_SCOPE_SWAY_MAX )
			shiftX = SGTC_SCOPE_SWAY_MAX;
		if( shiftX < -SGTC_SCOPE_SWAY_MAX )
			shiftX = -SGTC_SCOPE_SWAY_MAX;
		if( shiftY > SGTC_SCOPE_SWAY_MAX )
			shiftY = SGTC_SCOPE_SWAY_MAX;
		if( shiftY < -SGTC_SCOPE_SWAY_MAX )
			shiftY = -SGTC_SCOPE_SWAY_MAX;

		SGTC_Fx_DrawVignette( shiftX, shiftY );
		SGTC_Fx_DrawBreathMeter();
	}

	// Hit feedback stays centred on the reticle, scoped or not.
	SGTC_Fx_DrawHitmarks();
}

static void SGTC_SniperFx_ResetState( void )
{
	s_flFovTarget = 0.0f;
	s_flFovCur = 0.0f;
	s_bFovInit = false;

	for( int i = 0; i < SGTC_HITMARK_MAX; i++ )
	{
		s_hitmarks[i].time = 0.0f;
		s_hitmarks[i].flags = 0;
		s_hitmarks[i].active = false;
	}
	s_iHitmarkNext = 0;
}
