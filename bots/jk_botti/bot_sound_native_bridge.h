//
// Layout-compatible with dlls/soundent.h when BUILD_JK_BOTTI is defined.
// Do not include game headers here (Vector type differs from jk_botti vector.h).
//
#ifndef BOT_SOUND_NATIVE_BRIDGE_H
#define BOT_SOUND_NATIVE_BRIDGE_H

#include "extdll.h"

#define MAX_WORLD_SOUNDS 64
#define SOUNDLIST_EMPTY -1

class CSound
{
public:
	void Clear( void );
	void Reset( void );
	BOOL FIsSound( void );
	BOOL FIsScent( void );

	Vector m_vecOrigin;
	int m_iType;
	int m_iVolume;
	float m_flExpireTime;
	int m_iNext;
	int m_iNextAudible;
	edict_t *m_pEdict;
	int m_iChannel;
	int m_iBotOwner;
};

class CSoundEnt
{
public:
	void Spawn( void );
	void Think( void );
	void Initialize( void );
	void Precache( void );

	static void InsertSound( int iType, const Vector &vecOrigin, int iVolume, float flDuration );
	static void InsertSound( edict_t *pEdict, int channel, const Vector &vecOrigin, int iVolume, float flDuration, int iBotOwner );
	static void FreeSound( int iSound, int iPrevious );
	static int ActiveList( void );
	static int FreeList( void );
	static CSound *SoundPointerForIndex( int iIndex );
	static int ClientSoundIndex( edict_t *pClient );
	static CSound *GetEdictChannelSound( edict_t *pEdict, int iChannel );

	BOOL IsEmpty( void );
	int ISoundsInList( int iListType );
	int IAllocSound( void );
	int ObjectCaps( void );

	int m_iFreeSound;
	int m_iActiveSound;
	int m_cLastActiveSounds;
	BOOL m_fShowReport;

private:
	CSound m_SoundPool[MAX_WORLD_SOUNDS];
};

extern CSoundEnt *pSoundEnt;

#endif
