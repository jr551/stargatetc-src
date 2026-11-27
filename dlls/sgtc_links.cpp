// sgtc_links.cpp — StargateTC entity registrations for stock SDK classes the
// reference build reuses. Verified by disasm (GetClassPtr thunk targets):
//   weapon_c4  → CSatchel    (sgtc_wpn_c4.cpp, a CSatchel subclass; SGTC v_/p_/w_c4 models)
//   weapon_mine → CTripmine  (tripmine.cpp; SGTC v_/p_/w_mine models)
//   weapon_spas → CShotgun    (shotgun.cpp; SGTC v_/p_/w_spas + events/spas2.sc)

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
// CSatchel / CShotgun / CTripmine are all declared in weapons.h.
#include "sgtc_weapons.h"
#include "sgtc_wpn_c4.h"

LINK_ENTITY_TO_CLASS( weapon_c4, CSgtcC4 );
LINK_ENTITY_TO_CLASS( weapon_mine, CTripmine );
LINK_ENTITY_TO_CLASS( weapon_spas, CShotgun );
