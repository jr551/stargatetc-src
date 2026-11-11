// sgtc_token.h — CToken (game_nuke), the one and only definition.
//
// sgtc_token.cpp implements the class; sgtc_round.cpp calls ResetForNewRound()
// from its new-round sweep. Include this header instead of re-declaring a
// partial copy of the class (#97: differing definitions of one class in
// several translation units are an ODR violation).
//
// Needs extdll.h, util.h and cbase.h first.

#ifndef SGTC_TOKEN_H
#define SGTC_TOKEN_H

class CToken : public CBaseDelay
{
public:
	void Spawn( void );
	void Precache( void );
	void KeyValue( KeyValueData *pkvd );
	void Think( void );

	int Activator( CBaseEntity *pPlayer );
	void Deconax( void );
	void ResetForNewRound( void );	// ref NewRound: pev+0x160=30 then vslot +0xc4 (Think)
	int GetObjectiveState( void ) const;	// build-14 bot state: bomb state/armed/countdown packed

private:
	int m_iCountdown;	// ref this+0x19c (KeyValue "countdown")
	int m_iBombState;	// ref this+0x134 = 0xc5
	int m_iArmed;		// ref this+0x138 = 0
};

#endif // SGTC_TOKEN_H
