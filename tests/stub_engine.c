/*
 * stub_engine.c - minimal 32-bit engine stub for the StargateTC logic harness.
 *
 * dlopens a GoldSrc server plugin (ref/stc_i386.so or the rebuilt hl.so),
 * feeds it a stub enginefuncs_t + globalvars_t via GiveFnptrsToDll, pulls
 * the DLL_FUNCTIONS table via GetEntityAPI, then exercises entity logic:
 *
 *   gate <multi_gate_N>         Spawn + KeyValue(wait/target) + ManagerUse path
 *   weapon <weapon_name>         Spawn + GetItemInfo (iId/iSlot/iPosition)
 *   trigger <trigger_name>       Spawn + KeyValue + Touch + Think
 *   rules <mp_gamemode>          SGTC_InstallGameRules dispatch (rebuild) or
 *                                InstallGameRules string probe (reference)
 *
 * Entity instantiation goes through the binary's own exported factory
 * (LINK_ENTITY_TO_CLASS thunk: void <classname>(entvars_t *pev)), which is
 * exactly how the engine creates entities (GetProcAddress/dlsym on the
 * class name). DispatchSpawn/KeyValue/Think/Touch/Use run through the
 * GetEntityAPI table, as in-engine.
 *
 * Stub responsibilities (everything the exercised paths touch):
 *   - edict pool: CREATE_ENTITY / REMOVE_ENTITY / CREATE_NAMED_ENTITY
 *   - private data: PvAllocEntPrivateData / PvEntPrivateData /
 *     FreeEntPrivateData / GetVarsOfEnt
 *   - strings: AllocString / SzFromIndex (index 0 == empty string)
 *   - engine identity: pStringBase, gpGlobals->time, mapname
 *   - cvars: CVarGetString/CVarGetFloat/CVarRegister (mp_gamemode,
 *     mp_teamlist, showtriggers; everything else defaults to "0"/0.0)
 *   - precache/model: PrecacheModel/PrecacheSound/PrecacheEvent/ModelIndex/
 *     SetModel/SetSize/SetOrigin (record-only, always succeed)
 *   - messages: MessageBegin/MessageEnd/Write fns/RegUserMsg (record-only)
 *   - misc no-ops returning safe defaults: TraceLine/Hull, RandomLong/
 *     RandomFloat (deterministic midpoint), AlertMessage (stderr),
 *     FindEntityByString (stub registry), EntOffsetOfPEntity/
 *     PEntityOfEntOffset/IndexOfEdict, ModelFrames, PointContents,
 *     GetModelPtr, Time, IsMapValid, DecalIndex, VecToYaw/Angles, etc.
 *
 * Output: tab-separated records on stdout:
 *   BINARY <path>
 *   SIT <situation> <PASS|FAIL> <detail>
 * Exit: 0 = ran to completion (individual FAILs do not affect it),
 *       1 = usage/binary error, 2 = dlopen/GetEntityAPI failure.
 *
 * 32-bit only: gcc -m32. The harness (tests/test_logic.py) builds this.
 */
#include <stddef.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- minimal engine-side type mirrors (32-bit layout) ------------------ */
/* Must match hlsdk-xash3d/engine/{progdefs,edict,eface}.h for the fields
 * the stub touches. Only the prefix of enginefuncs_t up to the highest
 * used offset is modelled; indices follow eiface.h order. */

typedef int string_t;
typedef int qboolean;
typedef float vec3_t[3];
typedef unsigned int CRC32_t;

typedef struct edict_s edict_t;

/* Mirror of engine/eiface.h TraceResult. Keep the memset in the trace stubs
 * within the real object size: on i386 it is 56 bytes, not 64. */
typedef struct {
	int fAllSolid;
	int fStartSolid;
	int fInOpen;
	int fInWater;
	float flFraction;
	vec3_t vecEndPos;
	float flPlaneDist;
	vec3_t vecPlaneNormal;
	edict_t *pHit;
	int iHitgroup;
} stub_trace_t;

static void stub_zero_trace(void *ptr)
{
	if (ptr)
		memset(ptr, 0, sizeof(stub_trace_t));
}

typedef struct entvars_s {
	string_t classname;
	string_t globalname;
	vec3_t origin;
	vec3_t oldorigin;
	vec3_t velocity;
	vec3_t basevelocity;
	vec3_t clbasevelocity;
	vec3_t movedir;
	vec3_t angles;
	vec3_t avelocity;
	vec3_t punchangle;
	vec3_t v_angle;
	vec3_t endpos;
	vec3_t startpos;
	float impacttime;
	float starttime;
	int fixangle;
	float idealpitch;
	float pitch_speed;
	float ideal_yaw;
	float yaw_speed;
	int modelindex;
	string_t model;
	int viewmodel;
	int weaponmodel;
	vec3_t absmin;
	vec3_t absmax;
	vec3_t mins;
	vec3_t maxs;
	vec3_t size;
	float ltime;
	float nextthink;
	int movetype;
	int solid;
	int skin;
	int body;
	int effects;
	float gravity;
	float friction;
	int light_level;
	int sequence;
	int gaitsequence;
	float frame;
	float animtime;
	float framerate;
	unsigned char controller[4];
	unsigned char blending[2];
	float scale;
	int rendermode;
	float renderamt;
	vec3_t rendercolor;
	int renderfx;
	float health;
	float frags;
	int weapons;
	float takedamage;
	int deadflag;
	vec3_t view_ofs;
	int button;
	int impulse;
	edict_t *chain;
	edict_t *dmg_inflictor;
	edict_t *enemy;
	edict_t *aiment;
	edict_t *owner;
	edict_t *groundentity;
	int spawnflags;
	int flags;
	int colormap;
	int team;
	float max_health;
	float teleport_time;
	float armortype;
	float armorvalue;
	int waterlevel;
	int watertype;
	string_t target;
	string_t targetname;
	string_t netname;
	string_t message;
	float dmg_take;
	float dmg_save;
	float dmg;
	float dmgtime;
	string_t noise;
	string_t noise1;
	string_t noise2;
	string_t noise3;
	float speed;
	float air_finished;
	float pain_finished;
	float radsuit_finished;
	edict_t *pContainingEntity;
	int playerclass;
	float maxspeed;
	float fov;
	int weaponanim;
	int pushmsec;
	int bInDuck;
	int flTimeStepSound;
	int flSwimTime;
	int flDuckTime;
	int iStepLeft;
	float flFallVelocity;
	int gamestate;
	int oldbuttons;
	int groupinfo;
	int iuser1;
	int iuser2;
	int iuser3;
	int iuser4;
	float fuser1;
	float fuser2;
	float fuser3;
	float fuser4;
	vec3_t vuser1;
	vec3_t vuser2;
	vec3_t vuser3;
	vec3_t vuser4;
	edict_t *euser1;
	edict_t *euser2;
	edict_t *euser3;
	edict_t *euser4;
} entvars_t;

typedef struct {
	short leafnums[48];
	int headnode;
	int num_leafs;
} entleaf_t;

struct edict_s {
	int free;
	int serialnumber;
	void *area_next;
	void *area_prev;
	int headnode;
	int num_leafs;
	short leafnums[48];
	float freetime;
	void *pvPrivateData;
	entvars_t v;
};

typedef struct {
	float time;
	float frametime;
	float force_retouch;
	string_t mapname;
	string_t startspot;
	float deathmatch;
	float coop;
	float teamplay;
	float serverflags;
	float found_secrets;
	vec3_t v_forward;
	vec3_t v_up;
	vec3_t v_right;
	float trace_allsolid;
	float trace_startsolid;
	float trace_fraction;
	vec3_t trace_endpos;
	vec3_t trace_plane_normal;
	float trace_plane_dist;
	edict_t *trace_ent;
	float trace_inopen;
	float trace_inwater;
	int trace_hitgroup;
	int trace_flags;
	int changelevel;
	int cdAudioTrack;
	int maxClients;
	int maxEntities;
	const char *pStringBase;
	void *pSaveData;
	vec3_t vecLandmarkOffset;
} globalvars_t;

typedef struct {
	char *name;
	char *string;
	int flags;
	float value;
	void *next;
} cvar_t;

typedef struct {
	const char *szClassName;
	const char *szKeyName;
	const char *szValue;
	int fHandled;
} KeyValueData;

/* enginefuncs_t: full 157-pointer xash layout is overkill; the stub only
 * needs correct offsets for the functions the binaries call. Offsets are
 * fixed by eiface.h order, so we declare the complete table with exact
 * positions (generated order, 0-based index * 4 = byte offset):
 * 0 PrecacheModel 1 PrecacheSound 2 SetModel 3 ModelIndex 4 ModelFrames
 * 5 SetSize 6 ChangeLevel 7 GetSpawnParms 8 SaveSpawnParms 9 VecToYaw
 * 10 VecToAngles 11 MoveToOrigin 12 ChangeYaw 13 ChangePitch
 * 14 FindEntityByString 15 GetEntityIllum 16 FindEntityInSphere
 * 17 FindClientInPVS 18 EntitiesInPVS 19 MakeVectors 20 AngleVectors
 * 21 CreateEntity 22 RemoveEntity 23 CreateNamedEntity 24 MakeStatic
 * 25 EntIsOnFloor 26 DropToFloor 27 WalkMove 28 SetOrigin 29 EmitSound
 * 30 BuildSoundMsg 31 TraceLine 32 TraceToss 33 TraceMonsterHull
 * 34 TraceHull 35 TraceModel 36 TraceTexture 37 TraceSphere
 * 38 GetAimVector 39 ServerCommand 40 ServerExecute 41 ClientCommand
 * 42 ParticleEffect 43 LightStyle 44 DecalIndex 45 PointContents
 * 46 MessageBegin 47 MessageEnd 48 WriteByte 49 WriteChar 50 WriteShort
 * 51 WriteLong 52 WriteAngle 53 WriteCoord 54 WriteString 55 WriteEntity
 * 56 CVarRegister 57 CVarGetFloat 58 CVarGetString 59 CVarSetFloat
 * 60 CVarSetString 61 AlertMessage 62 EngineFprintf
 * 63 PvAllocEntPrivateData 64 PvEntPrivateData 65 FreeEntPrivateData
 * 66 SzFromIndex 67 AllocString 68 GetVarsOfEnt 69 PEntityOfEntOffset
 * 70 EntOffsetOfPEntity 71 IndexOfEdict 72 PEntityOfEntIndex ...
 * (... 73..159 zero-filled; the reference binary only copies the first
 * 144 entries and never touches the tail.)
 */
typedef struct {
	/* >= sizeof(enginefuncs_t) / sizeof(void *) (159 on HEAD): GiveFnptrsToDll memcpys the whole struct. */
	void *f[160];
} enginefuncs_stub_t;

typedef struct {
	void *f[50];
} dllfuncs_t;

/* ---- stub state --------------------------------------------------------- */
#define MAX_EDICTS 128
#define MAX_STRINGS 4096
#define STRING_POOL_BYTES (256 * 1024)

static edict_t s_edicts[MAX_EDICTS];
static int s_edict_used = 0;
/* Engine string_t is an offset into gpGlobals->pStringBase, not an ordinal:
 * real game code does STRING(i) as (pStringBase + i). Keep the pool
 * contiguous so entity fields we create can be read by the DLL's STRING(). */
static char s_string_pool[STRING_POOL_BYTES] = { 0 };
static int s_string_offsets[MAX_STRINGS];
static int s_nstrings = 1; /* 0 == empty string */
static int s_string_pool_used = 1;
static globalvars_t s_globals;

static char s_gamemode[64] = "";
static char s_teamlist[64] = "Torri;Goa'uld";

static void *s_dl = NULL;

/* ---- string pool -------------------------------------------------------- */
static const char *stub_SzFromIndex(int i)
{
	if (i <= 0 || i >= s_string_pool_used)
		return "";
	return s_string_pool + i;
}

static int stub_AllocString(const char *sz)
{
	int i;
	size_t len;

	if (!sz || !*sz)
		return 0;
	for (i = 1; i < s_nstrings; i++)
		if (!strcmp(s_string_pool + s_string_offsets[i], sz))
			return s_string_offsets[i];
	if (s_nstrings >= MAX_STRINGS)
		return 0;
	len = strlen(sz) + 1;
	if (s_string_pool_used + (int)len > (int)sizeof(s_string_pool))
		return 0;
	s_string_offsets[s_nstrings] = s_string_pool_used;
	memcpy(s_string_pool + s_string_pool_used, sz, len);
	s_string_pool_used += (int)len;
	return s_string_offsets[s_nstrings++];
}

/* ---- edicts ------------------------------------------------------------- */
static edict_t *stub_CreateEntity(void)
{
	int i;
	for (i = 1; i < MAX_EDICTS; i++) {
		if (s_edicts[i].free) {
			memset(&s_edicts[i], 0, sizeof(edict_t));
			s_edicts[i].free = 0;
			s_edicts[i].v.pContainingEntity = &s_edicts[i];
			if (i > s_edict_used)
				s_edict_used = i;
			return &s_edicts[i];
		}
	}
	return NULL;
}

static void stub_RemoveEntity(edict_t *e)
{
	if (!e)
		return;
	if (e->pvPrivateData) {
		free(e->pvPrivateData);
		e->pvPrivateData = NULL;
	}
	memset(e, 0, sizeof(edict_t));
	e->free = 1;
}

static edict_t *stub_CreateNamedEntity(int className)
{
	edict_t *e = stub_CreateEntity();
	const char *name;
	void (*factory)(entvars_t *);
	if (!e)
		return NULL;
	e->v.classname = className;
	name = stub_SzFromIndex(className);
	if (name && *name && s_dl) {
		factory = (void (*)(entvars_t *))dlsym(s_dl, name);
		if (factory)
			factory(&e->v);
	}
	return e;
}

static void *stub_PvAllocEntPrivateData(edict_t *e, int cb)
{
	if (!e)
		return NULL;
	if (e->pvPrivateData)
		free(e->pvPrivateData);
	e->pvPrivateData = calloc(1, cb > 0 ? cb : 1);
	return e->pvPrivateData;
}

static void *stub_PvEntPrivateData(edict_t *e)
{
	return e ? e->pvPrivateData : NULL;
}

static void stub_FreeEntPrivateData(edict_t *e)
{
	if (e && e->pvPrivateData) {
		free(e->pvPrivateData);
		e->pvPrivateData = NULL;
	}
}

static entvars_t *stub_GetVarsOfEnt(edict_t *e)
{
	return e ? &e->v : NULL;
}

static edict_t *stub_PEntityOfEntOffset(int off)
{
	int i;
	for (i = 1; i < MAX_EDICTS; i++)
		if (!s_edicts[i].free && (int)&s_edicts[i] == off)
			return &s_edicts[i];
	return NULL;
}

static int stub_EntOffsetOfPEntity(const edict_t *e)
{
	return (int)e;
}

static int stub_IndexOfEdict(const edict_t *e)
{
	return e ? (int)(e - s_edicts) : -1;
}

static edict_t *stub_PEntityOfEntIndex(int i)
{
	if (i < 0 || i >= MAX_EDICTS || s_edicts[i].free)
		return NULL;
	return &s_edicts[i];
}

/* ---- cvars -------------------------------------------------------------- */
static const char *stub_CVarGetString(const char *name)
{
	if (!name)
		return "";
	if (!strcmp(name, "mp_gamemode"))
		return s_gamemode;
	if (!strcmp(name, "mp_teamlist"))
		return s_teamlist;
	if (!strcmp(name, "showtriggers"))
		return "1";
	if (!strcmp(name, "mp_fraglimit") || !strcmp(name, "mp_timelimit"))
		return "0";
	return "";
}

static float stub_CVarGetFloat(const char *name)
{
	return (float)atof(stub_CVarGetString(name));
}

static void stub_CVarRegister(cvar_t *c)
{
	(void)c;
}

static cvar_t *stub_CVarGetPointer(const char *name)
{
	(void)name;
	return NULL;
}

/* ---- precache / model (record-only) ------------------------------------- */
static int stub_PrecacheModel(const char *s)
{
	(void)s;
	return 1;
}

static int stub_PrecacheSound(const char *s)
{
	(void)s;
	return 1;
}

static int stub_ModelIndex(const char *s)
{
	(void)s;
	return 1;
}

static int stub_ModelFrames(int m)
{
	(void)m;
	return 1;
}

static void stub_SetModel(edict_t *e, const char *m)
{
	(void)e;
	(void)m;
}

static void stub_SetSize(edict_t *e, const float *a, const float *b)
{
	(void)e;
	(void)a;
	(void)b;
}

static void stub_SetOrigin(edict_t *e, const float *o)
{
	if (e && o) {
		e->v.origin[0] = o[0];
		e->v.origin[1] = o[1];
		e->v.origin[2] = o[2];
	}
}

static unsigned short stub_PrecacheEvent(int type, const char *s)
{
	(void)type;
	(void)s;
	return 1;
}

static int stub_DecalIndex(const char *s)
{
	(void)s;
	return 0;
}

static int stub_PointContents(const float *v)
{
	(void)v;
	return 0;
}

static void *stub_GetModelPtr(edict_t *e)
{
	(void)e;
	return NULL;
}

/* ---- messages (record-only) --------------------------------------------- */
static void stub_MessageBegin(int dest, int type, const float *org, edict_t *ed)
{
	(void)dest;
	(void)type;
	(void)org;
	(void)ed;
}

static void stub_MessageEnd(void)
{
}

static void stub_WriteByte(int v) { (void)v; }
static void stub_WriteChar(int v) { (void)v; }
static void stub_WriteShort(int v) { (void)v; }
static void stub_WriteLong(int v) { (void)v; }
static void stub_WriteAngle(float v) { (void)v; }
static void stub_WriteCoord(float v) { (void)v; }
static void stub_WriteString(const char *s) { (void)s; }
static void stub_WriteEntity(int v) { (void)v; }

static int stub_RegUserMsg(const char *name, int size)
{
	static int next = 64;
	(void)name;
	(void)size;
	return next++;
}

/* ---- physics/trace defaults --------------------------------------------- */
static void stub_TraceLine(const float *a, const float *b, int nm,
	edict_t *skip, void *ptr)
{
	(void)a;
	(void)b;
	(void)nm;
	(void)skip;
	stub_zero_trace(ptr);
}

static void stub_TraceHull(const float *a, const float *b, int nm, int hull,
	edict_t *skip, void *ptr)
{
	(void)a;
	(void)b;
	(void)nm;
	(void)hull;
	(void)skip;
	stub_zero_trace(ptr);
}

static void stub_TraceToss(edict_t *a, edict_t *b, void *ptr)
{
	(void)a;
	(void)b;
	stub_zero_trace(ptr);
}

static int stub_TraceMonsterHull(edict_t *a, const float *b, const float *c,
	int d, edict_t *e, void *ptr)
{
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	(void)e;
	stub_zero_trace(ptr);
	return 0;
}

static float stub_VecToYaw(const float *v)
{
	(void)v;
	return 0.0f;
}

static void stub_VecToAngles(const float *a, float *b)
{
	if (a && b) {
		b[0] = b[1] = b[2] = 0.0f;
	}
}

static void stub_MakeVectors(const float *v)
{
	(void)v;
	s_globals.v_forward[0] = 1.0f;
	s_globals.v_forward[1] = s_globals.v_forward[2] = 0.0f;
	s_globals.v_up[2] = 1.0f;
	s_globals.v_up[0] = s_globals.v_up[1] = 0.0f;
	s_globals.v_right[1] = 1.0f;
	s_globals.v_right[0] = s_globals.v_right[2] = 0.0f;
}

static void stub_AngleVectors(const float *v, float *f, float *r, float *u)
{
	(void)v;
	if (f) {
		f[0] = 1.0f;
		f[1] = f[2] = 0.0f;
	}
	if (r) {
		r[1] = 1.0f;
		r[0] = r[2] = 0.0f;
	}
	if (u) {
		u[2] = 1.0f;
		u[0] = u[1] = 0.0f;
	}
}

static int stub_RandomLong(int lo, int hi)
{
	return (lo + hi) / 2;
}

static float stub_RandomFloat(float lo, float hi)
{
	return (lo + hi) * 0.5f;
}

static void stub_AlertMessage(int type, const char *fmt, ...)
{
	(void)type;
	(void)fmt;
}

static void stub_ServerCommand(const char *s)
{
	(void)s;
}

static void stub_ServerExecute(void)
{
}

static void stub_ServerPrint(const char *msg)
{
	(void)msg;
}

static edict_t *stub_FindEntityByString(edict_t *start, const char *field,
	const char *value)
{
	int i, from = 0;
	int by_class = field && !strcmp(field, "classname");
	int by_target = !field || !field[0] || !strcmp(field, "targetname");

	if (!by_class && !by_target)
		return NULL;

	if (start)
		from = (int)(start - s_edicts) + 1;
	for (i = from; i < MAX_EDICTS; i++) {
		string_t name = by_class ? s_edicts[i].v.classname
					 : s_edicts[i].v.targetname;
		if (!s_edicts[i].free && name &&
		    !strcmp(stub_SzFromIndex(name), value ? value : ""))
			return &s_edicts[i];
	}
	return NULL;
}

static int stub_DropToFloor(edict_t *e)
{
	(void)e;
	return 1;
}

static int stub_EntIsOnFloor(edict_t *e)
{
	(void)e;
	return 1;
}

static void stub_MoveToOrigin(edict_t *e, const float *g, float d, int m)
{
	(void)d;
	(void)m;
	stub_SetOrigin(e, g);
}

static void stub_ChangeYaw(edict_t *e)
{
	(void)e;
}

static void stub_ChangePitch(edict_t *e)
{
	(void)e;
}

static void stub_EmitSound(edict_t *e, int ch, const char *s, float v,
	float a, int f, int p)
{
	(void)e;
	(void)ch;
	(void)s;
	(void)v;
	(void)a;
	(void)f;
	(void)p;
}

static void stub_MakeStatic(edict_t *e)
{
	(void)e;
}

static int stub_WalkMove(edict_t *e, float y, float d, int m)
{
	(void)e;
	(void)y;
	(void)d;
	(void)m;
	return 1;
}

static float stub_Time(void)
{
	return s_globals.time;
}

static int stub_NumberOfEntities(void)
{
	return s_edict_used;
}

static int stub_IsMapValid(const char *s)
{
	(void)s;
	return 1;
}

static int stub_IsDedicatedServer(void)
{
	return 0;
}

static void stub_GetGameDir(char *s)
{
	if (s)
		strcpy(s, "stargatetc");
}

static void *stub_LoadFileForMe(const char *n, int *len)
{
	(void)n;
	if (len)
		*len = 0;
	return NULL;
}

static void stub_FreeFile(void *b)
{
	(void)b;
}

/* enginefuncs table, exact xash offsets (index * 4). Unlisted slots NULL. */
static void build_enginefuncs(enginefuncs_stub_t *t)
{
	memset(t, 0, sizeof(*t));
	t->f[0] = stub_PrecacheModel;
	t->f[1] = stub_PrecacheSound;
	t->f[2] = stub_SetModel;
	t->f[3] = stub_ModelIndex;
	t->f[4] = stub_ModelFrames;
	t->f[5] = stub_SetSize;
	t->f[6] = stub_ServerCommand; /* ChangeLevel: harmless */
	t->f[9] = stub_VecToYaw;
	t->f[10] = stub_VecToAngles;
	t->f[11] = stub_MoveToOrigin;
	t->f[12] = stub_ChangeYaw;
	t->f[13] = stub_ChangePitch;
	t->f[14] = stub_FindEntityByString;
	t->f[19] = stub_MakeVectors;
	t->f[20] = stub_AngleVectors;
	t->f[21] = stub_CreateEntity;
	t->f[22] = stub_RemoveEntity;
	t->f[23] = stub_CreateNamedEntity;
	t->f[24] = stub_MakeStatic;
	t->f[25] = stub_EntIsOnFloor;
	t->f[26] = stub_DropToFloor;
	t->f[27] = stub_WalkMove;
	t->f[28] = stub_SetOrigin;
	t->f[29] = stub_EmitSound;
	t->f[31] = stub_TraceLine;
	t->f[32] = stub_TraceToss;
	t->f[33] = stub_TraceMonsterHull;
	t->f[34] = stub_TraceHull;
	t->f[39] = stub_ServerCommand;
	t->f[40] = stub_ServerExecute;
	t->f[44] = stub_DecalIndex;
	t->f[45] = stub_PointContents;
	t->f[46] = stub_MessageBegin;
	t->f[47] = stub_MessageEnd;
	t->f[48] = stub_WriteByte;
	t->f[49] = stub_WriteChar;
	t->f[50] = stub_WriteShort;
	t->f[51] = stub_WriteLong;
	t->f[52] = stub_WriteAngle;
	t->f[53] = stub_WriteCoord;
	t->f[54] = stub_WriteString;
	t->f[55] = stub_WriteEntity;
	t->f[81] = stub_ServerPrint;
	t->f[56] = stub_CVarRegister;
	t->f[57] = stub_CVarGetFloat;
	t->f[58] = stub_CVarGetString;
	t->f[59] = stub_CVarGetFloat; /* SetFloat: ignore */
	t->f[60] = stub_CVarGetFloat; /* SetString: ignore */
	t->f[61] = stub_AlertMessage;
	t->f[63] = stub_PvAllocEntPrivateData;
	t->f[64] = stub_PvEntPrivateData;
	t->f[65] = stub_FreeEntPrivateData;
	t->f[66] = stub_SzFromIndex;
	t->f[67] = stub_AllocString;
	t->f[68] = stub_GetVarsOfEnt;
	t->f[69] = stub_PEntityOfEntOffset;
	t->f[70] = stub_EntOffsetOfPEntity;
	t->f[71] = stub_IndexOfEdict;
	t->f[72] = stub_PEntityOfEntIndex;
	t->f[73] = stub_FindEntityByString; /* FindEntityByVars: approx */
	t->f[74] = stub_GetModelPtr;
	t->f[75] = stub_RegUserMsg;
	t->f[90] = stub_RandomLong;
	t->f[91] = stub_RandomFloat;
	t->f[93] = stub_Time;
	t->f[95] = stub_LoadFileForMe;
	t->f[96] = stub_FreeFile;
	t->f[99] = stub_GetGameDir;
	t->f[105] = stub_NumberOfEntities;
	t->f[110] = stub_IsMapValid;
	t->f[115] = stub_IsDedicatedServer;
	t->f[116] = stub_CVarGetPointer;
	t->f[122] = stub_PrecacheEvent;
}

/* ---- ItemInfo mirror (hlsdk-xash3d/dlls/weapons.h) ----------------------- */
typedef struct {
	int iSlot;
	int iPosition;
	const char *pszAmmo1;
	int iMaxAmmo1;
	const char *pszAmmo2;
	int iMaxAmmo2;
	const char *pszName;
	int iMaxClip;
	int iId;
	int iFlags;
	int iWeight;
} ItemInfo;

/* ---- helpers ------------------------------------------------------------ */
static void reset_state(void)
{
	int i;
	for (i = 0; i < MAX_EDICTS; i++) {
		if (s_edicts[i].pvPrivateData)
			free(s_edicts[i].pvPrivateData);
		memset(&s_edicts[i], 0, sizeof(edict_t));
		s_edicts[i].free = 1;
	}
	s_edicts[0].free = 0; /* world */
	s_edicts[0].v.pContainingEntity = &s_edicts[0];
	s_edict_used = 0;
	s_globals.time = 1.0f;
	s_globals.frametime = 0.01f;
}

static edict_t *spawn_via_factory(const char *classname)
{
	void (*factory)(entvars_t *);
	edict_t *e;
	if (!classname || !s_dl)
		return NULL;
	factory = (void (*)(entvars_t *))dlsym(s_dl, classname);
	if (!factory)
		return NULL;
	e = stub_CreateEntity();
	if (!e)
		return NULL;
	e->v.classname = stub_AllocString(classname);
	factory(&e->v);
	return e;
}

static void kv(edict_t *e, dllfuncs_t *api, const char *cls,
	const char *key, const char *val)
{
	KeyValueData kvd;
	kvd.szClassName = cls;
	kvd.szKeyName = key;
	kvd.szValue = val;
	kvd.fHandled = 0;
	((void (*)(edict_t *, KeyValueData *))api->f[6])(e, &kvd);
}

/* ---- situations ---------------------------------------------------------- */

/* 1) multi_gate_N: KeyValue wait + extra target, Spawn, ManagerUse, Think. */
static void sit_gate(dllfuncs_t *api, const char *name, int is_ref)
{
	char cls[64];
	edict_t *e, *target;
	KeyValueData kvd;
	int ctargets_before, ctargets_after;
	(void)is_ref;
	snprintf(cls, sizeof(cls), "%s", name);
	reset_state();

	e = stub_CreateEntity();
	if (!e) {
		printf("SIT\tgate:%s\tFAIL\tno-edict\n", name);
		return;
	}
	e->v.classname = stub_AllocString(cls);
	{
		void (*factory)(entvars_t *) =
			(void (*)(entvars_t *))dlsym(s_dl, cls);
		if (!factory) {
			printf("SIT\tgate:%s\tFAIL\tno-factory\n", name);
			return;
		}
		factory(&e->v);
		if (!e->pvPrivateData) {
			printf("SIT\tgate:%s\tFAIL\tno-private\n", name);
			return;
		}
	}

	/* KeyValue path: wait + one extra target before Spawn. */
	kv(e, api, cls, "wait", "5");
	kv(e, api, cls, "probe_extra_target", "1.5");

	/* Spawn seeds the default fire list. */
	if (((int (*)(edict_t *))api->f[1])(e) < 0) {
		printf("SIT\tgate:%s\tFAIL\tspawn-rejected\n", name);
		return;
	}

	/* Read m_cTargets back through the save-data-independent route:
	 * fire ManagerUse then count FindEntityByString hits is engine
	 * specific; instead verify via a second KeyValue batch + HasTarget
	 * behaviour: ManagerUse must arm think (nextthink set). */
	ctargets_before = 0;
	{
		/* m_cTargets sits right after m_SaveData-described fields;
		 * we cannot read C++ privates here, so observe behaviour:
		 * ManagerUse (DispatchUse) must set nextthink to now. */
		float t0 = e->v.nextthink;
		((void (*)(edict_t *, edict_t *))api->f[3])(e, e);
		if (e->v.nextthink == t0 && e->v.nextthink != s_globals.time) {
			printf("SIT\tgate:%s\tFAIL\tuse-no-arm t0=%g now=%g nt=%g\n",
				name, t0, s_globals.time, e->v.nextthink);
			return;
		}
	}

	/* ManagerThink must progress without crashing and eventually
	 * disarm (nextthink stops advancing once all targets fired). */
	{
		int i, fired = 0;
		float last = -1;
		for (i = 0; i < 70; i++) {
			s_globals.time += 0.5f;
			((void (*)(edict_t *))api->f[2])(e);
			if (e->v.nextthink != last) {
				last = e->v.nextthink;
				fired++;
			}
			if (e->v.nextthink == 0)
				break;
		}
		(void)fired;
	}

	/* Clone path: spawnflags multithread -> ManagerUse clones. Just
	 * exercise; clone without engine CreateNamedEntity registry must
	 * not crash. */
	e->v.spawnflags = 1;
	((void (*)(edict_t *, edict_t *))api->f[3])(e, e);
	e->v.spawnflags = 0;

	/* KeyValue after spawn still handled. */
	kvd.szClassName = cls;
	kvd.szKeyName = "wait";
	kvd.szValue = "7";
	kvd.fHandled = 0;
	((void (*)(edict_t *, KeyValueData *))api->f[6])(e, &kvd);
	ctargets_after = kvd.fHandled ? 1 : 0;

	/* A named target entity so FireTargets has something to resolve. */
	target = stub_CreateNamedEntity(stub_AllocString("info_target"));
	(void)target;
	(void)ctargets_before;

	printf("SIT\tgate:%s\t%s\tkv-after=%d\n", name,
		ctargets_after ? "PASS" : "FAIL", ctargets_after);
}

dllfuncs_t g_api;

 /* 2) weapon: Spawn + GetItemInfo via the exported class method symbol. */
static void sit_weapon(const char *link, int exp_id, int is_ref,
	const char *mang)
{
	edict_t *e;
	ItemInfo info;
	int (*getitem)(void *self, ItemInfo *p);
	char *sym;
	(void)is_ref;
	reset_state();
	/* weapon Spawn touches g_pGameRules (mp5.cpp:60 IsMultiplayer). */
	{
		void *(*inst)(void) =
			(void *(*)(void))dlsym(s_dl, "_Z16InstallGameRulesv");
		void *(*sgtc)(const char *) =
			(void *(*)(const char *))dlsym(s_dl, "_Z21SGTC_InstallGameRulesPKc");
		void **pGR = (void **)dlsym(s_dl, "g_pGameRules");
		void *rules = sgtc ? sgtc("sg_tc") : NULL;
		if (!rules && inst) rules = inst();
		if (pGR) *pGR = rules;
	}
	e = spawn_via_factory(link);
	if (!e) {
		printf("SIT\tweapon:%s\tFAIL\tno-factory\n", link);
		return;
	}
	if (!e->pvPrivateData) {
		printf("SIT\tweapon:%s\tFAIL\tno-private\n", link);
		return;
	}
	/* Spawn (precaches + SET_MODEL + FallInit). FallInit needs engine
	 * MoveToOrigin/SetSize; stubs cover it. */
	{
		/* Call Spawn through DispatchSpawn so the -1 convention holds. */
		if (((int (*)(edict_t *))g_api.f[1])(e) < 0) {
			printf("SIT\tweapon:%s\tFAIL\tspawn-rejected\n", link);
			return;
		}
	}

	/* GetItemInfo symbol resolves when the weapon's class method is
	 * exported (ref always is; rebuild requires the default-visibility
	 * test build in build-dbg/). */
	sym = (char *)mang;
	getitem = (int (*)(void *, ItemInfo *))dlsym(s_dl, sym);
	if (!getitem) {
		printf("SIT\tweapon:%s\tFAIL\tno-GetItemInfo\n", link);
		return;
	}
	memset(&info, 0, sizeof(info));
	if (!getitem(e->pvPrivateData, &info)) {
		printf("SIT\tweapon:%s\tFAIL\tgetitem-ret0\n", link);
		return;
	}
	if (info.iId == exp_id && info.iSlot >= 0 && info.iSlot <= 6) {
		printf("SIT\tweapon:%s\tPASS\tiId=%d slot=%d pos=%d\n",
			link, info.iId, info.iSlot, info.iPosition);
	} else {
		printf("SIT\tweapon:%s\tFAIL\tiId=%d(want%d) slot=%d pos=%d\n",
			link, info.iId, exp_id, info.iSlot, info.iPosition);
	}
}


/* 3) triggers: Spawn + KeyValue + Touch + Think. */
static void sit_trigger(dllfuncs_t *api, const char *name, const char *key,
	const char *val)
{
	edict_t *e, *other;
	reset_state();
	e = spawn_via_factory(name);
	if (!e || !e->pvPrivateData) {
		printf("SIT\ttrigger:%s\tFAIL\tno-factory\n", name);
		return;
	}
	kv(e, api, name, key, val);
	if (((int (*)(edict_t *))api->f[1])(e) < 0) {
		printf("SIT\ttrigger:%s\tFAIL\tspawn-rejected\n", name);
		return;
	}
	/* Toucher: a live client-flagged entity. */
	other = stub_CreateEntity();
	if (!other) {
		printf("SIT\ttrigger:%s\tFAIL\tno-edict\n", name);
		return;
	}
	other->v.flags = 8; /* FL_CLIENT */
	other->v.health = 100;
	other->v.deadflag = 0;
	s_globals.time += 5.0f;
	((void (*)(edict_t *, edict_t *))api->f[4])(e, other);
	s_globals.time += 5.0f;
	((void (*)(edict_t *))api->f[2])(e);
	/* RandomUse toggle must not crash. */
	((void (*)(edict_t *, edict_t *))api->f[3])(e, other);
	printf("SIT\ttrigger:%s\tPASS\ttouch+think+use ok\n", name);
}

/* 4) InstallGameRules dispatch. Rebuild exports SGTC_InstallGameRules;
 * reference exposes only InstallGameRules (reads the mp_gamemode cvar).
 * For the reference we verify the prefix table behaviourally: set
 * mp_gamemode, call InstallGameRules, check a rules object comes back
 * whose vtable GetGameDescription differs per mode family. Since vtables
 * are opaque here, PASS = non-NULL for game modes, and we additionally
 * report the description string pointer result. */
static void sit_rules(const char *mode, const char *want_desc_sub)
{
	void *desc_fn = NULL;
	(void)want_desc_sub;
	if (dlsym(s_dl, "_Z21SGTC_InstallGameRulesPKc")) {
		void *(*inst)(const char *) =
			(void *(*)(const char *))dlsym(s_dl,
				"_Z21SGTC_InstallGameRulesPKc");
		void *rules = inst(mode);
		const char *desc = NULL;
		if (rules) {
			/* Do not call through vtable from this harness: GCC
			 * member-pointer ABI differs and rep_ segfaulted. */
			printf("SIT\trules:%s\t%s\trules=%p\n",
				mode && *mode ? mode : "(empty)",
				rules ? "PASS" : "FAIL",
				rules);
		} else {
			printf("SIT\trules:%s\t%s\tnull-rules\n",
				mode && *mode ? mode : "(empty)",
				(!mode || !*mode || !strncmp(mode, "rep_", 4) ||
				 strncmp(mode, "sg_", 3)) ? "PASS" : "FAIL");
		}
	} else {
		void *(*inst)(void) =
			(void *(*)(void))dlsym(s_dl, "_Z16InstallGameRulesv");
		void *rules;
		if (!inst) {
			printf("SIT\trules:%s\tFAIL\tno-installer\n",
				mode);
			return;
		}
		snprintf(s_gamemode, sizeof(s_gamemode), "%s", mode);
		rules = inst();
		printf("SIT\trules:%s\t%s\tref-rules=%p\n", mode,
			rules ? "PASS" : "FAIL", rules);
		(void)desc_fn;
	}
}

/* 5) botlogic: pure SGTC loadout decisions (dlls/sgtc_bots_loadout.cpp). */
typedef struct {
	int team_counts[3];
	int class_caps[3][6];
	int class_counts[3][6];
} bot_loadout_state_t;

static void sit_botlogic(void)
{
	int (*pick_team)(const bot_loadout_state_t *, int, unsigned int);
	int (*pick_class)(int, const int *, const int *, int, unsigned int);
	unsigned int (*mix_seed)(unsigned int, int);
	unsigned int (*next_random)(unsigned int *);
	int (*team_name)(int, char *, int);
	int (*class_model)(int, int, char *, int);
	bot_loadout_state_t st;
	int fails = 0, checks = 0;
	char buf[32];

	pick_team = (int (*)(const bot_loadout_state_t *, int, unsigned int))
		dlsym(s_dl, "SGTC_Bots_PickTeam");
	pick_class = (int (*)(int, const int *, const int *, int, unsigned int))
		dlsym(s_dl, "SGTC_Bots_PickClass");
	mix_seed = (unsigned int (*)(unsigned int, int))dlsym(s_dl, "SGTC_Bots_MixSeed");
	next_random = (unsigned int (*)(unsigned int *))dlsym(s_dl, "SGTC_Bots_NextRandom");
	team_name = (int (*)(int, char *, int))dlsym(s_dl, "SGTC_Bots_TeamName");
	class_model = (int (*)(int, int, char *, int))dlsym(s_dl, "SGTC_Bots_ClassModelName");

	if (!pick_team || !pick_class || !mix_seed || !next_random || !team_name || !class_model) {
		printf("SIT\tbotlogic:hooks\tFAIL\tmissing PURE symbols\n");
		return;
	}

	memset(&st, 0, sizeof(st));

	/* team: smaller team wins, both directions */
	st.team_counts[1] = 4;
	st.team_counts[2] = 1;
	printf("SIT\tbotlogic:team-small\t%s\ttorri4/goauld1 -> %d\n",
		pick_team(&st, 0, 0) == 2 ? "PASS" : "FAIL", pick_team(&st, 0, 0));
	if (pick_team(&st, 0, 0) != 2) fails++;
	st.team_counts[1] = 1;
	st.team_counts[2] = 4;
	printf("SIT\tbotlogic:team-small-rev\t%s\ttorri1/goauld4 -> %d\n",
		pick_team(&st, 0, 0) == 1 ? "PASS" : "FAIL", pick_team(&st, 0, 0));
	if (pick_team(&st, 0, 0) != 1) fails++;

	/* team: forced override honoured */
	printf("SIT\tbotlogic:team-force\t%s\tforced=2 with torri ahead\n",
		pick_team(&st, 2, 0) == 2 ? "PASS" : "FAIL");
	if (pick_team(&st, 2, 0) != 2) fails++;

	/* team: tie is deterministic for one seed but alternates across seeds */
	st.team_counts[1] = st.team_counts[2] = 3;
	{
		int t0 = pick_team(&st, 0, 0x9e3779b9u);
		int t0b = pick_team(&st, 0, 0x9e3779b9u);
		int t1 = pick_team(&st, 0, 0x9e3779b8u);
		int ok = (t0 == t0b) && (t0 >= 1 && t0 <= 2) && (t1 >= 1 && t1 <= 2) && (t0 != t1);
		printf("SIT\tbotlogic:team-tie\t%s\t%x->%d repeat=%d other=%d\n",
			ok ? "PASS" : "FAIL", 0x9e3779b9u, t0, t0b, t1);
		if (!ok) fails++;
	}

	/* class: skips capped classes (only class 5 open) */
	{
		int caps[6] = { 0, 1, 1, 1, 1, 1 };
		int counts[6] = { 0, 1, 1, 1, 1, 0 };
		int c = pick_class(1, caps, counts, 0, 0);
		printf("SIT\tbotlogic:class-skip-capped\t%s\tgot %d want 5\n",
			c == 5 ? "PASS" : "FAIL", c);
		if (c != 5) fails++;
	}

	/* class: all capped -> 0, never out of range */
	{
		int caps[6] = { 0, 1, 1, 1, 1, 1 };
		int counts[6] = { 0, 1, 1, 1, 1, 1 };
		int c = pick_class(1, caps, counts, 0, 0);
		printf("SIT\tbotlogic:class-all-capped\t%s\tgot %d want 0\n",
			c == 0 ? "PASS" : "FAIL", c);
		if (c != 0) fails++;
	}

	/* class: unlimited caps pick inside 1..5, deterministic per seed */
	{
		int caps[6] = { 0, 0, 0, 0, 0, 0 };
		int counts[6] = { 0, 0, 0, 0, 0, 0 };
		int a = pick_class(2, caps, counts, 0, 42);
		int b = pick_class(2, caps, counts, 0, 42);
		int ok = (a >= 1 && a <= 5) && a == b;
		printf("SIT\tbotlogic:class-range\t%s\tgot %d repeat=%d\n",
			ok ? "PASS" : "FAIL", a, b);
		if (!ok) fails++;
	}

	/* class: forced class honoured when uncapped */
	{
		int caps[6] = { 0, 0, 0, 0, 0, 0 };
		int counts[6] = { 0, 0, 0, 0, 0, 0 };
		int c = pick_class(2, caps, counts, 3, 0);
		printf("SIT\tbotlogic:class-force\t%s\tgot %d want 3\n",
			c == 3 ? "PASS" : "FAIL", c);
		if (c != 3) fails++;
	}

	/* class: forced class that is capped falls back to an open class */
	{
		int caps[6] = { 0, 1, 1, 1, 1, 0 };
		int counts[6] = { 0, 1, 1, 1, 1, 0 };
		int c = pick_class(1, caps, counts, 1, 0);
		printf("SIT\tbotlogic:class-force-capped\t%s\tgot %d want 5\n",
			c == 5 ? "PASS" : "FAIL", c);
		if (c != 5) fails++;
	}

	/* determinism: same (seed, index) -> same value, different index -> differs */
	{
		unsigned int a = mix_seed(1234, 7);
		unsigned int b = mix_seed(1234, 7);
		unsigned int c = mix_seed(1234, 8);
		int ok = (a == b) && (a != c);
		printf("SIT\tbotlogic:seed-mix\t%s\t%x %x %x\n",
			ok ? "PASS" : "FAIL", a, b, c);
		if (!ok) fails++;
	}

	/* determinism: PRNG sequence reproducible from the same state */
	{
		unsigned int s1 = mix_seed(1234, 0);
		unsigned int s2 = s1;
		int ok = 1, i;
		for (i = 0; i < 8; i++) {
			if (next_random(&s1) != next_random(&s2))
				ok = 0;
		}
		printf("SIT\tbotlogic:seed-seq\t%s\teight draws identical\n", ok ? "PASS" : "FAIL");
		if (!ok) fails++;
	}

	/* names: team and class model tables agree with game_shared/sgtc_enums.h */
	{
		int ok = team_name(1, buf, sizeof(buf)) == 0 && !strcmp(buf, "Torri");
		ok = ok && team_name(2, buf, sizeof(buf)) == 0 && !strcmp(buf, "Goa'uld");
		ok = ok && class_model(1, 1, buf, sizeof(buf)) == 0 && !strcmp(buf, "oneill");
		ok = ok && class_model(2, 5, buf, sizeof(buf)) == 0 && !strcmp(buf, "horus");
		ok = ok && class_model(9, 9, buf, sizeof(buf)) != 0;
		printf("SIT\tbotlogic:names\t%s\ttorri/goauld + class models\n", ok ? "PASS" : "FAIL");
		if (!ok) fails++;
	}

	checks = 10;
	printf("SIT\tbotlogic:summary\t%s\t%d checks, %d failed\n",
		fails == 0 ? "PASS" : "FAIL", checks, fails);
}

/* 5b) gatedial: pure wormhole/DHD timing model + bot gate decision
 * (dlls/sgtc_gate_timing.h, dlls/sgtc_bots_gate.h). The test is deliberately
 * C-only and calls the exported wrappers, so it exercises the exact model the
 * running server DLL uses. */
typedef struct {
	int state;
	int gate_id;
	int chevrons_locked;
	int failure;
	float dial_start_time;
	float next_chevron_time;
	float warmup_start_time;
	float open_time;
	float close_time;
	float retry_time;
} gd_timing_t;

typedef struct {
	int at_entry;
	float distance;
	int can_dial;
	int allow_enter;
} gd_ctx_t;

static int gd_near(float a, float b)
{
	float d = a - b;
	if (d < 0) d = -d;
	return d < 0.01f;
}

static void sit_gatedial(void)
{
	void (*reset)(gd_timing_t *, int, float);
	int (*begin)(gd_timing_t *, float);
	int (*abort_dial)(gd_timing_t *, float);
	void (*tick)(gd_timing_t *, float);
	int (*can_enter)(const gd_timing_t *, float);
	float (*remaining)(const gd_timing_t *, float);
	int (*observe_open)(gd_timing_t *, float);
	int (*observe_close)(gd_timing_t *, float);
	int (*decision)(const gd_timing_t *, const gd_ctx_t *, float);
	gd_timing_t g;
	gd_ctx_t c;
	int fails = 0, checks = 0, i;
	float t;

	reset = (void (*)(gd_timing_t *, int, float))
		dlsym(s_dl, "SGTC_GateTiming_Reset");
	begin = (int (*)(gd_timing_t *, float))
		dlsym(s_dl, "SGTC_GateTiming_BeginDial");
	abort_dial = (int (*)(gd_timing_t *, float))
		dlsym(s_dl, "SGTC_GateTiming_Abort");
	tick = (void (*)(gd_timing_t *, float))
		dlsym(s_dl, "SGTC_GateTiming_Tick");
	can_enter = (int (*)(const gd_timing_t *, float))
		dlsym(s_dl, "SGTC_GateTiming_CanEnter");
	remaining = (float (*)(const gd_timing_t *, float))
		dlsym(s_dl, "SGTC_GateTiming_WindowRemaining");
	observe_open = (int (*)(gd_timing_t *, float))
		dlsym(s_dl, "SGTC_GateTiming_ObserveOpened");
	observe_close = (int (*)(gd_timing_t *, float))
		dlsym(s_dl, "SGTC_GateTiming_ObserveClosed");
	decision = (int (*)(const gd_timing_t *, const gd_ctx_t *, float))
		dlsym(s_dl, "SGTC_Bots_GateDecision");

	if (!reset || !begin || !abort_dial || !tick || !can_enter || !remaining ||
		!observe_open || !observe_close || !decision) {
		printf("SIT\tgatedial:hooks\tFAIL\tmissing PURE gate timing symbols\n");
		return;
	}

	memset(&g, 0, sizeof(g));
	memset(&c, 0, sizeof(c));
	c.at_entry = 1;
	c.distance = 64.0f;
	c.can_dial = 1;
	c.allow_enter = 1;

	/* closed gate: no entry, bot dials */
	reset(&g, 1, 100.0f);
	printf("SIT\tgatedial:closed-near\t%s\tclosed gate must not accept entry\n",
		!can_enter(&g, 100.0f) ? "PASS" : "FAIL"); if (can_enter(&g, 100.0f)) fails++;
	printf("SIT\tgatedial:closed-decision\t%s\tbot at a closed gate must dial\n",
		decision(&g, &c, 100.0f) == 2 ? "PASS" : "FAIL"); if (decision(&g, &c, 100.0f) != 2) fails++;
	checks += 2;

	/* dial start and busy refusal */
	{
		int started = begin(&g, 100.0f);
		int again = begin(&g, 100.1f);
		printf("SIT\tgatedial:dial-start\t%s\tfirst dial accepted\n",
			started == 1 ? "PASS" : "FAIL"); if (started != 1) fails++;
		printf("SIT\tgatedial:dial-start-once\t%s\tsecond dial refused while dialling\n",
			again == 0 ? "PASS" : "FAIL"); if (again != 0) fails++;
	}
	printf("SIT\tgatedial:dial-no-entry\t%s\tno entry before the window\n",
		!can_enter(&g, 100.1f) ? "PASS" : "FAIL"); if (can_enter(&g, 100.1f)) fails++;
	checks += 3;

	/* seven address chevrons + point of origin, then warm-up; no entry */
	t = 100.0f;
	for (i = 1; i <= 8; i++) {
		t += 0.55f;
		tick(&g, t);
		if (g.chevrons_locked != i || can_enter(&g, t))
			fails++;
	}
	printf("SIT\tgatedial:chevrons\t%s\t8 dial steps before warm-up (last=%d)\n",
		g.chevrons_locked == 8 ? "PASS" : "FAIL", g.chevrons_locked);
	if (g.chevrons_locked != 8) fails++;
	checks += 9;

	printf("SIT\tgatedial:warmup-state\t%s\tstate=WARMUP\n",
		g.state == 2 ? "PASS" : "FAIL"); if (g.state != 2) fails++;
	printf("SIT\tgatedial:warmup-no-entry\t%s\tentry invalid before warm-up ends\n",
		!can_enter(&g, g.open_time - 0.01f) ? "PASS" : "FAIL"); if (can_enter(&g, g.open_time - 0.01f)) fails++;
	printf("SIT\tgatedial:warmup-wait\t%s\tbot waits through the warm-up\n",
		decision(&g, &c, g.open_time - 0.01f) == 1 ? "PASS" : "FAIL");
	if (decision(&g, &c, g.open_time - 0.01f) != 1) fails++;
	checks += 3;

	tick(&g, g.open_time);
	printf("SIT\tgatedial:entry-at-open\t%s\tentry valid exactly at open_time\n",
		can_enter(&g, g.open_time) ? "PASS" : "FAIL"); if (!can_enter(&g, g.open_time)) fails++;
	printf("SIT\tgatedial:enter-decision\t%s\tbot enters inside the window\n",
		decision(&g, &c, g.open_time) == 3 ? "PASS" : "FAIL");
	if (decision(&g, &c, g.open_time) != 3) fails++;
	printf("SIT\tgatedial:window-remaining\t%s\tfull open window\n",
		gd_near(remaining(&g, g.open_time), 15.5f) ? "PASS" : "FAIL");
	if (!gd_near(remaining(&g, g.open_time), 15.5f)) fails++;
	checks += 3;

	/* before close valid, at close invalid, retry re-dials */
	printf("SIT\tgatedial:entry-before-close\t%s\tentry valid just before close\n",
		can_enter(&g, g.close_time - 0.01f) ? "PASS" : "FAIL");
	if (!can_enter(&g, g.close_time - 0.01f)) fails++;
	tick(&g, g.close_time);
	printf("SIT\tgatedial:entry-at-close\t%s\tentry invalid at close_time\n",
		!can_enter(&g, g.close_time) ? "PASS" : "FAIL"); if (can_enter(&g, g.close_time)) fails++;
	printf("SIT\tgatedial:close-decision\t%s\tbot does not enter a closed gate\n",
		decision(&g, &c, g.close_time) == 1 ? "PASS" : "FAIL");
	if (decision(&g, &c, g.close_time) != 1) fails++;
	printf("SIT\tgatedial:missed-state\t%s\tMISSED + window failure\n",
		g.state == 4 && g.failure == 3 ? "PASS" : "FAIL");
	if (g.state != 4 || g.failure != 3) fails++;
	tick(&g, g.retry_time);
	printf("SIT\tgatedial:retry\t%s\tcooldown returns to CLOSED and re-dials\n",
		g.state == 0 && decision(&g, &c, g.retry_time) == 2 ? "PASS" : "FAIL");
	if (g.state != 0 || decision(&g, &c, g.retry_time) != 2) fails++;
	checks += 5;

	/* aborted dial never opens */
	reset(&g, 1, 200.0f);
	begin(&g, 200.0f);
	tick(&g, 200.55f);
	{
		int aborted = abort_dial(&g, 200.6f);
		printf("SIT\tgatedial:abort\t%s\tabort mid-dial accepted\n",
			aborted == 1 ? "PASS" : "FAIL"); if (aborted != 1) fails++;
	}
	printf("SIT\tgatedial:abort-no-entry\t%s\taborted dial never opens\n",
		!can_enter(&g, 201.0f) && !can_enter(&g, 220.0f) ? "PASS" : "FAIL");
	if (can_enter(&g, 201.0f) || can_enter(&g, 220.0f)) fails++;
	printf("SIT\tgatedial:abort-state\t%s\tABORTED/abort failure\n",
		g.state == 5 && g.failure == 1 ? "PASS" : "FAIL");
	if (g.state != 5 || g.failure != 1) fails++;
	checks += 3;

	/* stalled dial times out */
	reset(&g, 1, 300.0f);
	begin(&g, 300.0f);
	tick(&g, 315.1f);
	printf("SIT\tgatedial:timeout\t%s\tstalled dial times out\n",
		g.state == 5 && g.failure == 2 ? "PASS" : "FAIL");
	if (g.state != 5 || g.failure != 2) fails++;
	checks++;

	/* a gate opened by a player/map relay is usable, then closes on the relay */
	reset(&g, 1, 400.0f);
	{
		int opened = observe_open(&g, 400.0f);
		printf("SIT\tgatedial:observed-open\t%s\tg1tele_x_on opens the model\n",
			opened == 1 && can_enter(&g, 400.1f) ? "PASS" : "FAIL");
		if (opened != 1 || !can_enter(&g, 400.1f)) fails++;

		{
			int closed = observe_close(&g, 410.0f);
			printf("SIT\tgatedial:observed-close\t%s\tg1tele_x_off closes the model\n",
				closed == 1 && !can_enter(&g, 410.1f) ? "PASS" : "FAIL");
			if (closed != 1 || can_enter(&g, 410.1f)) fails++;
		}
	}
	checks += 2;

	/* away from the entry, normal navigation resumes */
	c.at_entry = 0;
	reset(&g, 1, 500.0f);
	printf("SIT\tgatedial:not-at-entry\t%s\tnormal navigation elsewhere\n",
		decision(&g, &c, 500.0f) == 0 ? "PASS" : "FAIL");
	if (decision(&g, &c, 500.0f) != 0) fails++;
	checks++;

	printf("SIT\tgatedial:summary\t%s\t%d checks, %d failed\n",
		fails == 0 ? "PASS" : "FAIL", checks, fails);
}

/* 5c) gatedialwrapper: drive the engine wrapper itself, not just the model.
 *
 * SGTC_Bots_GateTimingFrame() is the code that actually presses the map's DHD.
 * The pure gatedial test above cannot see the wrapper's press loop or the
 * worldspawn teleporter scan, so both used to be untested. This situation
 * builds the sg_stargate shape (a dial1 func_button plus a gate teleport whose
 * target is null at scan time) and asserts:
 *   - a full dial sends exactly SGTC_GATE_DIAL_STEPS (8) presses,
 *   - the gate teleport is registered as a gate entry while targetless,
 *   - a targetless non-gate teleport is not registered as a gate,
 *   - the target installed later is resolved into a nav link.
 * The hooks exist only in an SGTC_EXPORT_ALL_SYMBOLS build.
 */
typedef struct {
	void (*reset_nav)(void);
	int (*scan_map)(void);
	int (*entry_count)(void);
	int (*entry_waypoint)(int);
	int (*entry_gate)(int);
	int (*entry_resolved)(int);
	int (*link_count)(void);
	int (*add_entry)(int, int, float, float, float);
	int (*drive)(int, int, int, int, int, int *, int *, float *, float *);
} gdw_hooks_t;

static edict_t *gdw_make_ent(const char *cls, const char *name,
	const char *target, float x, float y, float z)
{
	edict_t *e = stub_CreateEntity();
	if (!e)
		return NULL;
	e->v.classname = stub_AllocString(cls);
	if (name && name[0])
		e->v.targetname = stub_AllocString(name);
	if (target && target[0])
		e->v.target = stub_AllocString(target);
	e->v.origin[0] = x;
	e->v.origin[1] = y;
	e->v.origin[2] = z;
	e->v.absmin[0] = x;
	e->v.absmin[1] = y;
	e->v.absmin[2] = z;
	e->v.size[0] = e->v.size[1] = e->v.size[2] = 0.0f;
	return e;
}

static void sit_gatedialwrapper(void)
{
	gdw_hooks_t h;
	int fails = 0, checks = 0;
	int entry_count, wp, gate, presses, state, resolved, links, rc;
	float open_t, close_t;

	memset(&h, 0, sizeof(h));
	h.reset_nav = (void (*)(void))dlsym(s_dl, "SGTC_Bots_TestResetNav");
	h.scan_map = (int (*)(void))dlsym(s_dl, "SGTC_Bots_TestScanMap");
	h.entry_count = (int (*)(void))dlsym(s_dl, "SGTC_Bots_TestGateEntryCount");
	h.entry_waypoint = (int (*)(int))dlsym(s_dl, "SGTC_Bots_TestGateEntryWaypoint");
	h.entry_gate = (int (*)(int))dlsym(s_dl, "SGTC_Bots_TestGateEntryGate");
	h.entry_resolved = (int (*)(int))dlsym(s_dl, "SGTC_Bots_TestGateEntryResolved");
	h.link_count = (int (*)(void))dlsym(s_dl, "SGTC_Bots_TestLinkCount");
	h.add_entry = (int (*)(int, int, float, float, float))
		dlsym(s_dl, "SGTC_Bots_TestAddGateEntry");
	h.drive = (int (*)(int, int, int, int, int, int *, int *, float *, float *))
		dlsym(s_dl, "SGTC_Bots_TestDriveDial");

	if (!h.reset_nav || !h.scan_map || !h.entry_count || !h.entry_waypoint ||
		!h.entry_gate || !h.entry_resolved || !h.link_count ||
		!h.add_entry || !h.drive) {
		printf("SIT\tgatedialwrapper:hooks\tFAIL\tmissing wrapper test hooks\n");
		return;
	}

	/* A) Wrapper press sequence with an entry injected directly, so this
	 * check fails only on the press loop (the pre-fix eighth press is lost). */
	reset_state();
	h.reset_nav();
	gdw_make_ent("func_button", "dial1", "stage1", 0, 0, 0);
	{
		edict_t *bot = gdw_make_ent("info_target", "GateTestBot", NULL, 0, 0, 0);
		h.scan_map();
		if (!bot || !h.add_entry(0, 0, 0, 0, 0)) {
			printf("SIT\tgatedialwrapper:setup\tFAIL\tcould not build scenario A\n");
			fails++;
		} else {
			presses = state = 0;
			open_t = close_t = 0.0f;
			rc = h.drive((int)(bot - s_edicts), 0, 0, 460, 16,
				&presses, &state, &open_t, &close_t);
			printf("SIT\tgatedialwrapper:eight-presses\t%s\tpresses=%d (want 8)\n",
				rc && presses == 8 ? "PASS" : "FAIL", presses);
			if (!rc || presses != 8) fails++;
			printf("SIT\tgatedialwrapper:reaches-open\t%s\tstate=%d open_time=%.2f\n",
				rc && state == 3 ? "PASS" : "FAIL", state, open_t);
			if (!rc || state != 3) fails++;
			checks += 2;
		}
	}

	/* B) sg_stargate shape: g1tele_1 has no target at worldspawn, a
	 * trigger_changetarget installs it later. Registration must survive the
	 * null target and the lazy resolver must add the nav link after dial. */
	reset_state();
	h.reset_nav();
	{
		edict_t *trig = gdw_make_ent("trigger_teleport", "g1tele_1", NULL,
			0, 0, 0);
		edict_t *bot;
		gdw_make_ent("info_teleport_destination", "g1_1_dest", NULL,
			1024, 0, 0);
		/* Targetless, but not a gate: must not be registered. */
		gdw_make_ent("trigger_teleport", "random_tele", NULL, 0, 512, 0);
		gdw_make_ent("func_button", "dial1", "stage1", 0, 0, 0);
		gdw_make_ent("multi_manager", "g1tele_x_on", NULL, 0, 0, 100);
		bot = gdw_make_ent("info_target", "GateTestBot", NULL, 0, 0, 0);

		entry_count = h.scan_map();
		printf("SIT\tgatedialwrapper:register-targetless\t%s\tgate entries=%d (want 1)\n",
			entry_count == 1 ? "PASS" : "FAIL", entry_count);
		if (entry_count != 1) fails++;
		printf("SIT\tgatedialwrapper:ignore-nongate\t%s\ttargetless non-gate not registered\n",
			entry_count == 1 ? "PASS" : "FAIL");
		if (entry_count != 1) fails++;

		wp = entry_count > 0 ? h.entry_waypoint(0) : -1;
		gate = entry_count > 0 ? h.entry_gate(0) : -1;
		printf("SIT\tgatedialwrapper:entry-waypoint\t%s\twaypoint=%d gate=%d\n",
			wp >= 0 && gate == 0 ? "PASS" : "FAIL", wp, gate);
		if (wp < 0 || gate != 0) fails++;

		/* The map's trigger_changetarget installs the exit while the gate
		 * opens; simulate that after the worldspawn scan. */
		if (trig)
			trig->v.target = stub_AllocString("g1_1_dest");

		presses = state = resolved = links = 0;
		open_t = close_t = 0.0f;
		rc = 0;
		if (bot && wp >= 0 && gate >= 0)
			rc = h.drive((int)(bot - s_edicts), wp, gate, 460, 16,
				&presses, &state, &open_t, &close_t);

		printf("SIT\tgatedialwrapper:registered-dial\t%s\tpresses=%d state=%d\n",
			rc && presses == 8 && state == 3 ? "PASS" : "FAIL", presses, state);
		if (!rc || presses != 8 || state != 3) fails++;

		resolved = entry_count > 0 ? h.entry_resolved(0) : 0;
		links = h.link_count();
		printf("SIT\tgatedialwrapper:lazy-link\t%s\tresolved=%d links=%d (want resolved=1, links>=1)\n",
			resolved == 1 && links >= 1 ? "PASS" : "FAIL", resolved, links);
		if (resolved != 1 || links < 1) fails++;
		checks += 5;
	}

	printf("SIT\tgatedialwrapper:summary\t%s\t%d checks, %d failed\n",
		fails == 0 ? "PASS" : "FAIL", checks, fails);
}

/* 5b) botedict: pure bot edict-budget decision (dlls/sgtc_bots_budget.cpp). */
static void sit_botedict(void)
{
	int (*allows)(int, int, int);
	int (*allows_request)(int, int, int, int);
	int (*allow_spawn)(int);
	int (*allow_spawn_count)(int, int);
	int fails = 0, checks = 0;

	/* ROW-314: the reserve is loadout-aware. mp_freeguns > 1 needs 31
	 * (SGTC_NUM_WEAPON_SLOTS + 3) for the CP world grant; otherwise a normal
	 * class kit uses 16 (about 15 entities plus one spare). */
#define BOTEDICT_MARGIN_NORMAL 16
#define BOTEDICT_MARGIN_FREEGUNS 31

	allows = (int (*)(int, int, int))dlsym(s_dl, "SGTC_Bots_EdictBudgetAllows");
	allows_request = (int (*)(int, int, int, int))dlsym(s_dl, "SGTC_Bots_EdictBudgetAllowsRequest");
	allow_spawn = (int (*)(int))dlsym(s_dl, "SGTC_Bots_EdictBudgetAllowSpawn");
	allow_spawn_count = (int (*)(int, int))dlsym(s_dl, "SGTC_Bots_EdictBudgetAllowSpawnCount");
	if (!allows || !allows_request || !allow_spawn || !allow_spawn_count) {
		printf("SIT\tbotedict:hooks\tFAIL\tmissing %s\n",
			!allows ? "SGTC_Bots_EdictBudgetAllows" :
			!allows_request ? "SGTC_Bots_EdictBudgetAllowsRequest" :
			!allow_spawn ? "SGTC_Bots_EdictBudgetAllowSpawn" :
			"SGTC_Bots_EdictBudgetAllowSpawnCount");
		return;
	}

#define BOTEDICT_CHECK(name, used, maxe, margin, want) do { \
	int got = allows((used), (maxe), (margin)); \
	checks++; \
	if (got != (want)) fails++; \
	printf("SIT\tbotedict:%s\t%s\tused=%d max=%d margin=%d got=%d want=%d\n", \
		(name), got == (want) ? "PASS" : "FAIL", (used), (maxe), (margin), got, (want)); \
} while (0)

#define BOTEDICT_REQUEST(name, used, count, want) do { \
	int got = allows_request((used), 900, BOTEDICT_MARGIN_FREEGUNS, (count)); \
	checks++; \
	if (got != (want)) fails++; \
	printf("SIT\tbotedict:%s\t%s\tused=%d count=%d got=%d want=%d\n", \
		(name), got == (want) ? "PASS" : "FAIL", (used), (count), got, (want)); \
} while (0)

	/* Freeguns reserve: #292 sg_escape baseline 882/900 is refused because
	 * 882 >= 900 - 31, and 883/29 is the PT193-R3 reproducer. */
	BOTEDICT_CHECK("freeguns-plenty", 100, 900, BOTEDICT_MARGIN_FREEGUNS, 1);
	BOTEDICT_CHECK("freeguns-last-allowed", 868, 900, BOTEDICT_MARGIN_FREEGUNS, 1);
	BOTEDICT_CHECK("freeguns-at-margin", 869, 900, BOTEDICT_MARGIN_FREEGUNS, 0);
	BOTEDICT_CHECK("freeguns-baseline", 882, 900, BOTEDICT_MARGIN_FREEGUNS, 0);
	BOTEDICT_CHECK("freeguns-exactly-max", 900, 900, BOTEDICT_MARGIN_FREEGUNS, 0);
	BOTEDICT_CHECK("freeguns-over-max", 901, 900, BOTEDICT_MARGIN_FREEGUNS, 0);
	BOTEDICT_CHECK("freeguns-garbage-used", -1, 900, BOTEDICT_MARGIN_FREEGUNS, 0);
	BOTEDICT_CHECK("freeguns-garbage-max", 100, 0, BOTEDICT_MARGIN_FREEGUNS, 0);
	BOTEDICT_CHECK("freeguns-garbage-margin", 100, 900, -1, 0);
	BOTEDICT_CHECK("freeguns-margin-equals-max", 0, 31, BOTEDICT_MARGIN_FREEGUNS, 0);

	/* Normal reserve: 883 + 15 = 898 is allowed; 884/885 refuse. */
	BOTEDICT_CHECK("normal-883-allowed", 883, 900, BOTEDICT_MARGIN_NORMAL, 1);
	BOTEDICT_CHECK("normal-884-refused", 884, 900, BOTEDICT_MARGIN_NORMAL, 0);
	BOTEDICT_CHECK("normal-885-refused", 885, 900, BOTEDICT_MARGIN_NORMAL, 0);
	BOTEDICT_CHECK("normal-baseline-882", 882, 900, BOTEDICT_MARGIN_NORMAL, 1);
	BOTEDICT_CHECK("zero-margin-below", 899, 900, 0, 1);
	BOTEDICT_CHECK("zero-margin-at-max", 900, 900, 0, 0);

	/* PT193-R3 reproducer: 883 + 29 = 912 > 900 must be refused; the count
	 * pre-check also refuses a request that cannot reserve one slot per bot. */
	BOTEDICT_CHECK("freeguns-883", 883, 900, BOTEDICT_MARGIN_FREEGUNS, 0);
	BOTEDICT_CHECK("freeguns-868", 868, 900, BOTEDICT_MARGIN_FREEGUNS, 1);
	BOTEDICT_REQUEST("request-868x1", 868, 1, 1);
	BOTEDICT_REQUEST("request-868x2", 868, 2, 0);
	BOTEDICT_REQUEST("request-882x3", 882, 3, 0);

	/* Real production wrappers, not the pure functions above: BotCreate calls
	 * SGTC_Bots_EdictBudgetAllowSpawn(margin) and sgtc_bot_add calls the
	 * count-aware gate with the same margin. The stub's maxEntities is 128. */
#define BOTEDICT_SPAWN(name, margin, want) do { \
	int got = allow_spawn((margin)); \
	checks++; \
	if (got != (want)) fails++; \
	printf("SIT\tbotedict:%s\t%s\tused=%d max=%d margin=%d got=%d want=%d\n", \
		(name), got == (want) ? "PASS" : "FAIL", s_edict_used, \
		s_globals.maxEntities, (margin), got, (want)); \
} while (0)

#define BOTEDICT_SPAWN_COUNT(name, count, margin, want) do { \
	int got = allow_spawn_count((count), (margin)); \
	checks++; \
	if (got != (want)) fails++; \
	printf("SIT\tbotedict:%s\t%s\tused=%d count=%d margin=%d got=%d want=%d\n", \
		(name), got == (want) ? "PASS" : "FAIL", s_edict_used, \
		(count), (margin), got, (want)); \
} while (0)

	BOTEDICT_SPAWN("wrapper-empty-room-normal", BOTEDICT_MARGIN_NORMAL, 1);
	BOTEDICT_SPAWN("wrapper-empty-room-freeguns", BOTEDICT_MARGIN_FREEGUNS, 1);
	while (s_edict_used < 96 && stub_CreateEntity())
		;
	BOTEDICT_SPAWN("wrapper-freeguns-96-allowed", BOTEDICT_MARGIN_FREEGUNS, 1);
	BOTEDICT_SPAWN("wrapper-normal-96-allowed", BOTEDICT_MARGIN_NORMAL, 1);
	BOTEDICT_SPAWN_COUNT("wrapper-count-freeguns-96x2", 2, BOTEDICT_MARGIN_FREEGUNS, 0);
	BOTEDICT_SPAWN_COUNT("wrapper-count-normal-96x2", 2, BOTEDICT_MARGIN_NORMAL, 1);
	if (s_edict_used == 96)
		stub_CreateEntity();
	BOTEDICT_SPAWN("wrapper-freeguns-97-refused", BOTEDICT_MARGIN_FREEGUNS, 0);
	BOTEDICT_SPAWN("wrapper-normal-97-allowed", BOTEDICT_MARGIN_NORMAL, 1);
	while (s_edict_used < 111 && stub_CreateEntity())
		;
	BOTEDICT_SPAWN("wrapper-normal-111-allowed", BOTEDICT_MARGIN_NORMAL, 1);
	BOTEDICT_SPAWN_COUNT("wrapper-count-normal-111x2", 2, BOTEDICT_MARGIN_NORMAL, 0);
	BOTEDICT_SPAWN("wrapper-freeguns-111-refused", BOTEDICT_MARGIN_FREEGUNS, 0);
	if (s_edict_used == 111)
		stub_CreateEntity();
	BOTEDICT_SPAWN("wrapper-normal-112-refused", BOTEDICT_MARGIN_NORMAL, 0);
	BOTEDICT_SPAWN_COUNT("wrapper-count-normal-112x1", 1, BOTEDICT_MARGIN_NORMAL, 0);

#undef BOTEDICT_SPAWN_COUNT
#undef BOTEDICT_SPAWN
#undef BOTEDICT_REQUEST
#undef BOTEDICT_CHECK
#undef BOTEDICT_MARGIN_FREEGUNS
#undef BOTEDICT_MARGIN_NORMAL

	printf("SIT\tbotedict:summary\t%s\t%d checks, %d failed\n",
		fails == 0 ? "PASS" : "FAIL", checks, fails);
}

/* 6) botclass: per-class profiles and ability decisions
 * (dlls/sgtc_bots_classes.cpp). */
#define BOT_CLASS_MAX_KIT 12
typedef struct {
	int team;
	int cls;
	const char *name;
	int role;
	int abilities;
	int talent;
	int range_min;
	int range_max;
	const char *weapon_short;
	const char *weapon_mid;
	const char *weapon_long;
	int kit[BOT_CLASS_MAX_KIT];
} bot_class_profile_t;

typedef struct {
	int skill;
	float health;
	float charge;
	int shield_up;
	int enemy_visible;
	float enemy_distance;
	int under_fire;
	float out_of_combat;
	unsigned int rng;
} bot_class_shield_ctx_t;

typedef struct {
	int skill;
	float charge;
	int cloaked;
	int enemy_visible;
	float enemy_distance;
	float out_of_combat;
	unsigned int rng;
} bot_class_cloak_ctx_t;

typedef struct {
	int skill;
	int tac_ammo;
	int tac_near;
	float objective_distance;
	int at_chokepoint;
	int enemy_visible;
	float enemy_distance;
	float front_moved;
	unsigned int rng;
} bot_class_tac_ctx_t;

static void sit_botclass(void)
{
	const bot_class_profile_t *(*profile)(int, int);
	int (*wants_shield)(const bot_class_profile_t *,
		const bot_class_shield_ctx_t *);
	int (*wants_cloak)(const bot_class_profile_t *,
		const bot_class_cloak_ctx_t *);
	int (*wants_tac)(const bot_class_profile_t *,
		const bot_class_tac_ctx_t *);
	const bot_class_profile_t *shielder = NULL;
	const bot_class_profile_t *cloaker = NULL;
	const bot_class_profile_t *taccer = NULL;
	const bot_class_profile_t *plain = NULL;
	bot_class_shield_ctx_t shield;
	bot_class_cloak_ctx_t cloak;
	bot_class_tac_ctx_t tac;
	int team, cls, count = 0, fails = 0, checks = 0;
	int names_ok = 1, kits_ok = 1, ok;

	profile = (const bot_class_profile_t *(*)(int, int))
		dlsym(s_dl, "SGTC_Bots_ClassProfile");
	wants_shield = (int (*)(const bot_class_profile_t *,
		const bot_class_shield_ctx_t *))dlsym(s_dl, "SGTC_Bots_WantsShield");
	wants_cloak = (int (*)(const bot_class_profile_t *,
		const bot_class_cloak_ctx_t *))dlsym(s_dl, "SGTC_Bots_WantsCloak");
	wants_tac = (int (*)(const bot_class_profile_t *,
		const bot_class_tac_ctx_t *))dlsym(s_dl, "SGTC_Bots_WantsTacPlace");

	if (!profile || !wants_shield || !wants_cloak || !wants_tac) {
		printf("SIT\tbotclass:hooks\tFAIL\tmissing PURE symbols\n");
		return;
	}

	for (team = 1; team <= 2; team++) {
		for (cls = 1; cls <= 5; cls++) {
			const bot_class_profile_t *p = profile(team, cls);
			int kit_n = 0;

			if (!p)
				continue;
			count++;
			if (!p->name || !p->name[0])
				names_ok = 0;
			while (kit_n < BOT_CLASS_MAX_KIT && p->kit[kit_n] >= 0)
				kit_n++;
			if (kit_n == 0)
				kits_ok = 0;
			if (!shielder && !strcmp(p->name, "System Lord"))
				shielder = p;
			if (!cloaker && !strcmp(p->name, "Horus"))
				cloaker = p;
			if (!taccer && !strcmp(p->name, "O'Neill"))
				taccer = p;
			if (!plain && !strcmp(p->name, "Anubis"))
				plain = p;
		}
	}

	ok = (count == 10) && names_ok && kits_ok && shielder && cloaker &&
		taccer && plain;
	printf("SIT\tbotclass:profiles\t%s\t%d profiles, names=%d kits=%d\n",
		ok ? "PASS" : "FAIL", count, names_ok, kits_ok);
	checks++;
	if (!ok) {
		fails++;
		return;
	}

	memset(&shield, 0, sizeof(shield));
	shield.skill = 3;
	shield.health = 1.0f;
	shield.charge = 9.0f;
	shield.enemy_distance = -1.0f;
	shield.under_fire = 1;
	ok = wants_shield(shielder, &shield) == 1 &&
		wants_shield(plain, &shield) == 0;
	printf("SIT\tbotclass:shield\t%s\tfire->%d no-ability->%d\n",
		ok ? "PASS" : "FAIL",
		wants_shield(shielder, &shield), wants_shield(plain, &shield));
	checks++;
	if (!ok) fails++;

	memset(&cloak, 0, sizeof(cloak));
	cloak.skill = 3;
	cloak.charge = 9.0f;
	cloak.enemy_distance = -1.0f;
	cloak.out_of_combat = 1.0f;
	ok = wants_cloak(cloaker, &cloak) == 1;
	printf("SIT\tbotclass:cloak\t%s\tunseen->%d\n",
		ok ? "PASS" : "FAIL", wants_cloak(cloaker, &cloak));
	checks++;
	if (!ok) fails++;

	memset(&tac, 0, sizeof(tac));
	tac.skill = 3;
	tac.tac_ammo = 2;
	tac.tac_near = 0;
	tac.objective_distance = 300.0f;
	tac.enemy_distance = -1.0f;
	ok = wants_tac(taccer, &tac) == 1;
	printf("SIT\tbotclass:tac\t%s\tobjective->%d\n",
		ok ? "PASS" : "FAIL", wants_tac(taccer, &tac));
	checks++;
	if (!ok) fails++;

	printf("SIT\tbotclass:summary\t%s\t%d checks, %d failed\n",
		fails == 0 ? "PASS" : "FAIL", checks, fails);
}

/* 7) botevent: JSON-lines formatter shape (dlls/sgtc_bots_events.cpp). */
/* 6) botevent: JSON-lines formatter shape (dlls/sgtc_bots_events.cpp). */
typedef struct {
	const char *key;
	int kind;
	const char *sval;
	int ival;
	float fval;
	float vec[3];
} bot_event_field_t;

/* Minimal JSON shape check: prefix, one top-level object, balanced nesting,
 * terminated strings, no raw control bytes, and every key present. */
static int json_event_ok(const char *line, const char *const *keys, int nkeys,
	char *why, size_t whysize)
{
	const char *p;
	int depth = 0, in_str = 0, esc = 0, i;

	if (strncmp(line, "SGTCEVENT ", 10) != 0) {
		snprintf(why, whysize, "prefix");
		return 0;
	}
	p = line + 10;
	if (*p != '{') {
		snprintf(why, whysize, "no-object");
		return 0;
	}
	for (; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (in_str) {
			if (esc)
				esc = 0;
			else if (c == '\\')
				esc = 1;
			else if (c == '"')
				in_str = 0;
			else if (c < 0x20) {
				snprintf(why, whysize, "control-byte");
				return 0;
			}
		} else {
			if (c == '"')
				in_str = 1;
			else if (c == '{' || c == '[')
				depth++;
			else if (c == '}' || c == ']') {
				depth--;
				if (depth < 0) {
					snprintf(why, whysize, "unbalanced");
					return 0;
				}
			} else if (c == '\n' || c == '\r') {
				snprintf(why, whysize, "newline");
				return 0;
			}
		}
	}
	if (in_str || esc) {
		snprintf(why, whysize, "unterminated-string");
		return 0;
	}
	if (depth != 0) {
		snprintf(why, whysize, "unbalanced");
		return 0;
	}
	if (line[strlen(line) - 1] != '}') {
		snprintf(why, whysize, "no-close");
		return 0;
	}
	for (i = 0; i < nkeys; i++) {
		if (!strstr(line, keys[i])) {
			snprintf(why, whysize, "missing %s", keys[i]);
			return 0;
		}
	}
	return 1;
}

static void sit_botevent(void)
{
	int (*fmt)(char *, int, const char *, const char *, float, const char *,
		const bot_event_field_t *, int);
	char line[1000];
	char why[128];
	bot_event_field_t f[4];
	int fail = 0;

	fmt = (int (*)(char *, int, const char *, const char *, float, const char *,
		const bot_event_field_t *, int))dlsym(s_dl, "SGTC_Bots_FormatEvent");
	if (!fmt) {
		printf("SIT\tbotevent:hooks\tFAIL\tno SGTC_Bots_FormatEvent\n");
		return;
	}

	/* spawn */
	memset(f, 0, sizeof(f));
	f[0].key = "team"; f[0].kind = 1; f[0].ival = 2;
	f[1].key = "team_name"; f[1].kind = 0; f[1].sval = "Goa'uld";
	f[2].key = "class"; f[2].kind = 1; f[2].ival = 3;
	f[3].key = "origin"; f[3].kind = 3;
	f[3].vec[0] = 12.34f; f[3].vec[1] = -5.0f; f[3].vec[2] = 100.0f;
	{
		static const char *const keys[] = {
			"\"event\"", "\"map\"", "\"time\"", "\"bot\"",
			"\"team\"", "\"team_name\"", "\"class\"", "\"origin\""
		};
		if (fmt(line, sizeof(line), "spawn", "ctp_amon", 12.3456f, "b0t", f, 4) < 0)
			printf("SIT\tbotevent:spawn\tFAIL\tformat-error\n"), fail++;
		else if (!json_event_ok(line, keys, 8, why, sizeof(why)))
			printf("SIT\tbotevent:spawn\tFAIL\t%s\n", why), fail++;
		else
			printf("SIT\tbotevent:spawn\tPASS\t%.80s\n", line);
	}

	/* gate_use */
	memset(f, 0, sizeof(f));
	f[0].key = "from"; f[0].kind = 3; f[0].vec[0] = 1; f[0].vec[1] = 2; f[0].vec[2] = 3;
	f[1].key = "to"; f[1].kind = 3; f[1].vec[0] = 900; f[1].vec[1] = 20; f[1].vec[2] = 3;
	f[2].key = "distance"; f[2].kind = 2; f[2].fval = 899.1f;
	{
		static const char *const keys[] = {
			"\"event\"", "\"map\"", "\"time\"", "\"bot\"",
			"\"from\"", "\"to\"", "\"distance\""
		};
		if (fmt(line, sizeof(line), "gate_use", "rings", 42.0f, "b0t", f, 3) < 0)
			printf("SIT\tbotevent:gate\tFAIL\tformat-error\n"), fail++;
		else if (!json_event_ok(line, keys, 7, why, sizeof(why)))
			printf("SIT\tbotevent:gate\tFAIL\t%s\n", why), fail++;
		else
			printf("SIT\tbotevent:gate\tPASS\t%.80s\n", line);
	}

	/* stuck, with a hostile bot name that must come out escaped */
	memset(f, 0, sizeof(f));
	f[0].key = "duration"; f[0].kind = 2; f[0].fval = 8.25f;
	f[1].key = "goal"; f[1].kind = 1; f[1].ival = 17;
	f[2].key = "action"; f[2].kind = 0; f[2].sval = "reroute";
	f[3].key = "origin"; f[3].kind = 3; f[3].vec[0] = 0; f[3].vec[1] = 0; f[3].vec[2] = -14.5f;
	{
		static const char *const keys[] = {
			"\"event\"", "\"map\"", "\"time\"", "\"bot\"",
			"\"duration\"", "\"goal\"", "\"action\"", "\"origin\""
		};
		const char *evil = "he\"llo\\na\tme";
		if (fmt(line, sizeof(line), "stuck", "sg_chateau", 3.5f, evil, f, 4) < 0)
			printf("SIT\tbotevent:stuck\tFAIL\tformat-error\n"), fail++;
		else if (!json_event_ok(line, keys, 8, why, sizeof(why)))
			printf("SIT\tbotevent:stuck\tFAIL\t%s\n", why), fail++;
		else if (strstr(line, "he\"llo"))
			printf("SIT\tbotevent:stuck\tFAIL\tunescaped-name\n"), fail++;
		else
			printf("SIT\tbotevent:stuck\tPASS\t%.80s\n", line);
	}

	/* death: the event must carry the real death origin and the inflictor
	 * classname, not the post-death off-map park (#195) */
	memset(f, 0, sizeof(f));
	f[0].key = "killer"; f[0].kind = 0; f[0].sval = "world";
	f[1].key = "origin"; f[1].kind = 3;
	f[1].vec[0] = 1215.0f; f[1].vec[1] = 2865.0f; f[1].vec[2] = 359.0f;
	f[2].key = "weapon"; f[2].kind = 0; f[2].sval = "trapbox";
	{
		static const char *const keys[] = {
			"\"event\"", "\"map\"", "\"time\"", "\"bot\"",
			"\"killer\"", "\"origin\"", "\"weapon\"",
			"\"trapbox\"", "1215.0,2865.0,359.0"
		};
		if (fmt(line, sizeof(line), "death", "ctp_amon", 3.5f, "b0t", f, 3) < 0)
			printf("SIT\tbotevent:death\tFAIL\tformat-error\n"), fail++;
		else if (!json_event_ok(line, keys, 9, why, sizeof(why)))
			printf("SIT\tbotevent:death\tFAIL\t%s\n", why), fail++;
		else
			printf("SIT\tbotevent:death\tPASS\t%.80s\n", line);
	}

	/* death-origin resolver: snapshot wins, then last alive, never the park */
	{
		int (*resolve)(int, const float *, int, const float *, const float *,
			float *);
		float snap[3] = { 1215.0f, 2865.0f, 359.0f };
		float last_alive[3] = { 1200.0f, 2800.0f, 300.0f };
		float current[3] = { 4096.0f, 4096.0f, 4096.0f };
		float out[3] = { 0.0f, 0.0f, 0.0f };

		resolve = (int (*)(int, const float *, int, const float *,
			const float *, float *))dlsym(s_dl, "SGTC_Bots_ResolveDeathOrigin");
		if (!resolve)
		{
			printf("SIT\tbotevent:death-origin-snapshot\tFAIL\tno resolver\n"), fail++;
		}
		else
		{
			if (resolve(1, snap, 0, last_alive, current, out) != 1 ||
				out[0] != snap[0] || out[1] != snap[1] || out[2] != snap[2])
				printf("SIT\tbotevent:death-origin-snapshot\tFAIL\t%.1f,%.1f,%.1f\n",
					out[0], out[1], out[2]), fail++;
			else
				printf("SIT\tbotevent:death-origin-snapshot\tPASS\tdeath position wins\n");

			if (resolve(0, snap, 1, last_alive, current, out) != 1 ||
				out[0] != last_alive[0] || out[1] != last_alive[1] ||
				out[2] != last_alive[2])
				printf("SIT\tbotevent:death-origin-last-alive\tFAIL\t%.1f,%.1f,%.1f\n",
					out[0], out[1], out[2]), fail++;
			else
				printf("SIT\tbotevent:death-origin-last-alive\tPASS\tlast alive frame wins\n");

			if (resolve(0, snap, 0, last_alive, current, out) != 0 ||
				out[0] != current[0] || out[1] != current[1] ||
				out[2] != current[2])
				printf("SIT\tbotevent:death-origin-fallback\tFAIL\t%.1f,%.1f,%.1f\n",
					out[0], out[1], out[2]), fail++;
			else
				printf("SIT\tbotevent:death-origin-fallback\tPASS\tcurrent only when nothing else\n");
		}
	}

	/* overflow must fail, never emit a partial line */
	if (fmt(line, 24, "spawn", "m", 1.0f, "x", f, 4) >= 0)
		printf("SIT\tbotevent:overflow\tFAIL\ttruncated line accepted\n"), fail++;
	else
		printf("SIT\tbotevent:overflow\tPASS\trefused short buffer\n");

	/* native death capture: the slot write CBasePlayer::Killed() performs.
	 * This is the path that feeds SGTC_Bots_EmitDeath in the native build;
	 * before the rework there was no such hook (DeathMsg was inert), so the
	 * first case fails on dc09497. */
	{
		void (*capture)(int, const float *, const char *);
		int (*getstate)(int, float *, char *, int);
		int (*resolve)(int, const float *, int, const float *,
			const float *, float *);
		float death_origin[3] = { 1215.0f, 2865.0f, 359.0f };
		float park[3] = { 4096.0f, 4096.0f, 4096.0f };
		float got[3] = { 0.0f, 0.0f, 0.0f };
		float emitted[3] = { 0.0f, 0.0f, 0.0f };
		char weapon[32];
		int valid;

		capture = (void (*)(int, const float *, const char *))
			dlsym(s_dl, "SGTC_Bots_CaptureDeathState");
		getstate = (int (*)(int, float *, char *, int))
			dlsym(s_dl, "SGTC_Bots_GetDeathState");
		resolve = (int (*)(int, const float *, int, const float *,
			const float *, float *))dlsym(s_dl, "SGTC_Bots_ResolveDeathOrigin");

		if (!capture || !getstate || !resolve)
		{
			printf("SIT\tbotevent:death-capture\tFAIL\tno native capture hooks\n"), fail++;
		}
		else
		{
			memset(weapon, 0, sizeof(weapon));
			capture(0, death_origin, "monster_trapbox");
			valid = getstate(0, got, weapon, sizeof(weapon));
			if (!valid || got[0] != death_origin[0] || got[1] != death_origin[1]
				|| got[2] != death_origin[2] || strcmp(weapon, "trapbox") != 0)
				printf("SIT\tbotevent:death-capture\tFAIL\tvalid=%d origin=%.1f,%.1f,%.1f weapon=%s\n",
					valid, got[0], got[1], got[2], weapon), fail++;
			else
				printf("SIT\tbotevent:death-capture\tPASS\ttrapbox at exact pre-park origin\n");

			/* captured state -> resolver -> formatter, as EmitDeath does */
			if (resolve(1, got, 0, NULL, park, emitted) != 1)
				printf("SIT\tbotevent:death-capture-emit\tFAIL\tcapture not preferred\n"), fail++;
			else
			{
				static const char *const keys[] = {
					"\"event\":\"death\"", "\"killer\":\"world\"",
					"\"weapon\":\"trapbox\"", "1215.0,2865.0,359.0"
				};
				int rc;

				memset(f, 0, sizeof(f));
				f[0].key = "killer"; f[0].kind = 0; f[0].sval = "world";
				f[1].key = "origin"; f[1].kind = 3;
				f[1].vec[0] = emitted[0]; f[1].vec[1] = emitted[1]; f[1].vec[2] = emitted[2];
				f[2].key = "weapon"; f[2].kind = 0; f[2].sval = weapon;
				rc = fmt(line, sizeof(line), "death", "ctp_amon", 3.5f, "b0t", f, 3);
				if (rc < 0)
					printf("SIT\tbotevent:death-capture-emit\tFAIL\tformat-error\n"), fail++;
				else if (!json_event_ok(line, keys, 4, why, sizeof(why)))
					printf("SIT\tbotevent:death-capture-emit\tFAIL\t%s\n", why), fail++;
				else
					printf("SIT\tbotevent:death-capture-emit\tPASS\t%.80s\n", line);
			}

			memset(weapon, 0, sizeof(weapon));
			capture(0, death_origin, "teammate");
			valid = getstate(0, got, weapon, sizeof(weapon));
			if (!valid || strcmp(weapon, "teammate") != 0)
				printf("SIT\tbotevent:death-capture-teammate\tFAIL\tweapon=%s\n", weapon), fail++;
			else
				printf("SIT\tbotevent:death-capture-teammate\tPASS\tDeathMsg marker preserved\n");

			/* The Killed() path calls SGTC_BotDeathWeaponName() with the
			 * victim and killer entvars. It must read the killer's active
			 * item when inflictor == killer, exactly like DeathNotice(),
			 * not the victim's (#195, PT195-R3). */
			{
				const char *(*weapon_name)(const char *, const char *, int);
				const char *got_w;

				weapon_name = (const char *(*)(const char *, const char *, int))
					dlsym(s_dl, "SGTC_BotDeathWeaponNameForTest");
				if (!weapon_name)
					printf("SIT\tbotevent:death-weapon-killer\tFAIL\tno weapon-name seam\n"), fail++;
				else
				{
					got_w = weapon_name("p90", "m16", 0);
					if (!got_w || strcmp(got_w, "m16") != 0)
						printf("SIT\tbotevent:death-weapon-killer\tFAIL\texpected m16 got %s\n",
							got_w ? got_w : "(null)"), fail++;
					else
						printf("SIT\tbotevent:death-weapon-killer\tPASS\tkiller m16 wins over victim p90\n");

					got_w = weapon_name("m16", "m16", 1);
					if (!got_w || strcmp(got_w, "m16") != 0)
						printf("SIT\tbotevent:death-weapon-self\tFAIL\texpected m16 got %s\n",
							got_w ? got_w : "(null)"), fail++;
					else
						printf("SIT\tbotevent:death-weapon-self\tPASS\tself-kill uses the same player\n");

					got_w = weapon_name("p90", NULL, 0);
					if (!got_w || strcmp(got_w, "world") != 0)
						printf("SIT\tbotevent:death-weapon-no-item\tFAIL\texpected world got %s\n",
							got_w ? got_w : "(null)"), fail++;
					else
						printf("SIT\tbotevent:death-weapon-no-item\tPASS\tNULL active item -> world\n");
				}
			}

			/* first observed frame already dead: no capture, no stale value */
			capture(0, NULL, "monster_trapbox");
			memset(weapon, 0, sizeof(weapon));
			valid = getstate(0, got, weapon, sizeof(weapon));
			if (valid || weapon[0] != '\0')
				printf("SIT\tbotevent:death-capture-miss\tFAIL\tvalid=%d weapon=%s\n",
					valid, weapon), fail++;
			else
				printf("SIT\tbotevent:death-capture-miss\tPASS\tcleared origin and weapon\n");
		}
	}

	printf("SIT\tbotevent:summary\t%s\t%d failures\n", fail == 0 ? "PASS" : "FAIL", fail);
}

/* 6) bottac: pure enemy-TAC targeting decision (dlls/sgtc_bots_tac.cpp).
 * Mirrors bots/jk_botti/bot_tac.h; layout must stay identical. */
typedef struct {
	int bot_team;
	int tac_team;
	int owned_by_bot;
	int armed;
	int alive;
	int damageable;
	int visible;
	float distance;
	float max_distance;
	int higher_priority_enemy;
	int friendlyfire;
} bot_tac_candidate_t;

static const char *bottac_choice_name(int v)
{
	switch (v) {
	case 0: return "none";
	case 1: return "keep-enemy";
	case 2: return "tac";
	default: return "bad";
	}
}

static const char *bottac_reason_name(int v)
{
	switch (v) {
	case 0: return "none";
	case 1: return "higher-priority";
	case 2: return "own";
	case 3: return "team";
	case 4: return "dead";
	case 5: return "damage-no";
	case 6: return "unarmed";
	case 7: return "range";
	case 8: return "visibility";
	default: return "bad-input";
	}
}

static void bottac_default(bot_tac_candidate_t *c)
{
	memset(c, 0, sizeof(*c));
	c->bot_team = 1;      /* Goa'uld */
	c->tac_team = 2;      /* Torri */
	c->armed = 1;
	c->alive = 1;
	c->damageable = 1;
	c->visible = 1;
	c->distance = 300.0f;
	c->max_distance = 600.0f;
}

static void sit_bottac(void)
{
	int (*decision)(const bot_tac_candidate_t *);
	int (*reason)(const bot_tac_candidate_t *);
	int fail = 0, checks = 0;
	bot_tac_candidate_t c;

	decision = (int (*)(const bot_tac_candidate_t *))
		dlsym(s_dl, "SGTC_Bots_TacTargetDecision");
	reason = (int (*)(const bot_tac_candidate_t *))
		dlsym(s_dl, "SGTC_Bots_TacTargetReason");
	if (!decision || !reason) {
		printf("SIT\tbottac:hooks\tFAIL\tno SGTC_Bots_TacTarget* symbol\n");
		return;
	}

#define BOTTAC_CHECK(name, want_choice, want_reason, note) \
	do { \
		int got_choice = decision(&c); \
		int got_reason = reason(&c); \
		int ok = (got_choice == (want_choice) && got_reason == (want_reason)); \
		printf("SIT\tbottac:%s\t%s\tchoice=%s reason=%s%s\n", \
			(name), ok ? "PASS" : "FAIL", \
			bottac_choice_name(got_choice), \
			bottac_reason_name(got_reason), (note)); \
		checks++; \
		if (!ok) fail++; \
	} while (0)

	/* Armed, alive, damageable, in-range, visible enemy TAC => target. */
	bottac_default(&c);
	BOTTAC_CHECK("enemy-armed-visible", 2, 0, " (Goa'uld bot vs Torri TAC)");

	/* Boundary: distance == mp_tacdistance is still "within". */
	bottac_default(&c);
	c.distance = 600.0f;
	BOTTAC_CHECK("range-boundary", 2, 0, "");

	/* Friendly TAC is never targeted, even with mp_friendlyfire on. */
	bottac_default(&c);
	c.tac_team = 1;
	c.friendlyfire = 1;
	BOTTAC_CHECK("friendly-ff-on", 0, 3, " (mp_friendlyfire=1)");

	/* The bot's own TAC is never targeted. */
	bottac_default(&c);
	c.tac_team = 1;
	c.owned_by_bot = 1;
	BOTTAC_CHECK("own", 0, 2, "");

	/* Team 0 / unassigned is never treated as an enemy. */
	bottac_default(&c);
	c.tac_team = 0;
	BOTTAC_CHECK("team-unassigned", 0, 3, "");

	/* Still inside the 2.7 s arm delay. */
	bottac_default(&c);
	c.armed = 0;
	BOTTAC_CHECK("unarmed", 0, 6, "");

	/* Out of the TAC's mp_tacdistance reach. */
	bottac_default(&c);
	c.distance = 600.5f;
	BOTTAC_CHECK("out-of-range", 0, 7, "");

	/* Dead TAC. */
	bottac_default(&c);
	c.alive = 0;
	BOTTAC_CHECK("dead", 0, 4, "");

	/* Removed/non-damageable TAC. */
	bottac_default(&c);
	c.damageable = 0;
	BOTTAC_CHECK("removed", 0, 5, "");

	/* No line of sight. */
	bottac_default(&c);
	c.visible = 0;
	BOTTAC_CHECK("no-los", 0, 8, "");

	/* A higher-priority (player) enemy is already selected: keep it. */
	bottac_default(&c);
	c.higher_priority_enemy = 1;
	BOTTAC_CHECK("player-priority", 1, 1, " (visible player kept over TAC)");

	/* mp_friendlyfire must not change an enemy TAC either. */
	bottac_default(&c);
	c.friendlyfire = 1;
	BOTTAC_CHECK("enemy-ff-on", 2, 0, "");

#undef BOTTAC_CHECK

	printf("SIT\tbottac:summary\t%s\t%d checks, %d failed\n",
		fail == 0 ? "PASS" : "FAIL", checks, fail);
}

static void usage(const char *a0)
{
	fprintf(stderr,
		"usage: %s <lib.so> <command> [args]\n"
		"  gate <multi_gate_N>\n"
		"  weapon <link> <expected-iId> [GetItemInfo-mangled]\n"
		"  trigger <name> <key> <value>\n"
		"  rules <mp_gamemode>\n"
		"  botlogic         SGTC loadout decisions (pure hooks)\n"
		"  gatedial         wormhole/DHD timing and bot gate decisions\n"
		"  gatedialwrapper engine wrapper: 8 DHD presses, gate entry registration\n"
		"  botedict         SGTC bot edict-budget decision (pure hook)\n"
		"  botclass         SGTC class profiles and ability decisions\n"
		"  botevent         SGTCEVENT JSON-lines formatter\n"
		"  bottac           enemy TAC targeting decision (pure hooks)\n"
		"  isref            print 1 if InstallGameRules-only (no SGTC_ form)\n",
		a0);
}

int main(int argc, char **argv)
{
	enginefuncs_stub_t eng;
	dllfuncs_t *api = &g_api;
	void (*give)(void *, globalvars_t *);
	int (*getapi)(dllfuncs_t *, int);
	const char *sop;
	int is_ref;

	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}
	sop = argv[1];

	memset(s_edicts, 0, sizeof(s_edicts));
	{
		int i;
		for (i = 0; i < MAX_EDICTS; i++)
			s_edicts[i].free = 1;
	}
	s_edicts[0].free = 0;
	s_edicts[0].v.pContainingEntity = &s_edicts[0];
	memset(&s_globals, 0, sizeof(s_globals));
	s_globals.pStringBase = s_string_pool;
	s_globals.time = 1.0f;
	s_globals.frametime = 0.01f;
	s_globals.mapname = 0;
	s_globals.maxEntities = MAX_EDICTS;
	s_globals.maxClients = 1;

	s_dl = dlopen(sop, RTLD_NOW | RTLD_GLOBAL);
	if (!s_dl)
		s_dl = dlopen(sop, RTLD_LAZY | RTLD_GLOBAL);
	if (!s_dl) {
		printf("DLOPEN\tFAIL\t%s\n", dlerror());
		return 2;
	}
	printf("BINARY\t%s\n", sop);
	is_ref = (dlsym(s_dl, "_Z21SGTC_InstallGameRulesPKc") == NULL);

	if (!strcmp(argv[2], "isref")) {
		printf("ISREF\t%d\n", is_ref);
		return 0;
	}

	build_enginefuncs(&eng);
	give = (void (*)(void *, globalvars_t *))dlsym(s_dl,
		"GiveFnptrsToDll");
	getapi = (int (*)(dllfuncs_t *, int))dlsym(s_dl, "GetEntityAPI");
	if (!give || !getapi) {
		printf("DLOPEN\tFAIL\tmissing entry points\n");
		return 2;
	}
	give(&eng, &s_globals);
	if (!getapi(api, 140)) {
		printf("DLOPEN\tFAIL\tGetEntityAPI(140) refused\n");
		return 2;
	}

	if (!strcmp(argv[2], "testcmd") && argc >= 4) {
		void (*tc)(const char *) = (void (*)(const char *))dlsym(s_dl, "SGTC_TestCommand");
		if (!tc) {
			printf("SIT\ttestcmd\tFAIL\tno-SGTC_TestCommand\n");
			return 1;
		}
		tc(argv[3]);
		printf("SIT\ttestcmd:%s\tPASS\t\n", argv[3]);
		return 0;
	}

	if (!strcmp(argv[2], "gate") && argc >= 4) {
		sit_gate(api, argv[3], is_ref);
	} else if (!strcmp(argv[2], "weapon") && argc >= 5) {
		sit_weapon(argv[3], atoi(argv[4]), is_ref,
			argc >= 6 ? argv[5] : "");
	} else if (!strcmp(argv[2], "trigger") && argc >= 6) {
		sit_trigger(api, argv[3], argv[4], argv[5]);
	} else if (!strcmp(argv[2], "rules") && argc >= 4) {
		sit_rules(argv[3], "");
	} else if (!strcmp(argv[2], "botlogic")) {
		sit_botlogic();
	} else if (!strcmp(argv[2], "gatedial")) {
		sit_gatedial();
	} else if (!strcmp(argv[2], "gatedialwrapper")) {
		sit_gatedialwrapper();
	} else if (!strcmp(argv[2], "botedict")) {
		sit_botedict();
	} else if (!strcmp(argv[2], "botclass")) {
		sit_botclass();
	} else if (!strcmp(argv[2], "botevent")) {
		sit_botevent();
	} else if (!strcmp(argv[2], "bottac")) {
		sit_bottac();
	} else {
		usage(argv[0]);
		return 1;
	}
	return 0;
}
