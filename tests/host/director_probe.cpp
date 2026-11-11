// tests/host/director_probe.cpp - live host probe for the bot director.
//
// Posts one request with the real dlls/sgtc_director_core.cpp (system prompt,
// request builder, content extraction, finish_reason, plan parser) and
// dlls/sgtc_director_http.cpp (libcurl/WinHTTP client) against an
// OpenAI-compatible endpoint, then prints ONE line of JSON describing the
// result. It is the engine-free half of tests/director_live_probe.py.
//
//   director_probe --url URL --model NAME --doc FILE [--max-tokens N]
//                  [--temperature-x100 35] [--extra 'FRAGMENT']
//                  [--timeout-ms N] [--dump-content FILE]
//
// The API key comes from SGTC_BOTDIRECTOR_KEY in the environment only: it is
// never an argv value, never written to stdout/stderr and never dumped. The
// request body and the response body are never printed either; only lengths
// and parsed plan values leave this process.
//
// Usage note: this harness is for a manual, opt-in live run. It is not part
// of the feature registry and must never be invoked from CI.
//
// Build (tests/director_live_probe.py does this):
//   c++ -std=c++11 -O2 -Wall -Wextra -Idlls -pthread -o probe \
//       tests/host/director_probe.cpp dlls/sgtc_director_core.cpp \
//       dlls/sgtc_director_http.cpp

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "sgtc_director_core.h"
#include "sgtc_director_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#define PROBE_DOC_MAX (64 * 1024)
#define PROBE_BODY_MAX 32768
#define PROBE_RESP_MAX (64 * 1024 + 1)
#define PROBE_PUMP_LIMIT_MS 120000

struct ProbeResult
{
	int calls;
	int status;
	int body_len;
	char body[PROBE_RESP_MAX];
	char error[256];
};

struct DocBot
{
	int used;
	int team;
	int cls;
};

static ProbeResult g_res;
static DocBot g_doc_bots[SGTC_DIR_MAX_BOTS + 1];

static double now_ms(void)
{
#if defined(_WIN32)
	return (double)GetTickCount64();
#else
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

static void sleep_ms(int ms)
{
#if defined(_WIN32)
	Sleep((DWORD)ms);
#else
	usleep((useconds_t)(ms * 1000));
#endif
}

static void probe_done(void *ctx, int status, const char *body, int body_len,
	const char *error)
{
	ProbeResult *r = (ProbeResult *)ctx;
	int n = body_len;

	if (r == NULL)
		return;

	r->calls++;
	r->status = status;
	r->body_len = body_len;

	if (n < 0)
		n = 0;
	if (n > (int)sizeof(r->body) - 1)
		n = (int)sizeof(r->body) - 1;
	if (body != NULL && n > 0)
		memcpy(r->body, body, (size_t)n);
	r->body[n] = '\0';

	r->error[0] = '\0';
	if (error != NULL)
	{
		strncpy(r->error, error, sizeof(r->error) - 1);
		r->error[sizeof(r->error) - 1] = '\0';
	}
}

static void json_string(const char *s)
{
	const unsigned char *p = (const unsigned char *)(s ? s : "");

	putchar('"');
	for (; *p != '\0'; p++)
	{
		unsigned char c = *p;

		switch (c)
		{
		case '"': fputs("\\\"", stdout); break;
		case '\\': fputs("\\\\", stdout); break;
		case '\b': fputs("\\b", stdout); break;
		case '\f': fputs("\\f", stdout); break;
		case '\n': fputs("\\n", stdout); break;
		case '\r': fputs("\\r", stdout); break;
		case '\t': fputs("\\t", stdout); break;
		default:
			if (c < 0x20)
				printf("\\u%04x", (unsigned int)c);
			else
				putchar((int)c);
			break;
		}
	}
	putchar('"');
}

static const char *role_name(int role)
{
	switch (role)
	{
	case SGTC_DIR_ROLE_ATTACK: return "attack";
	case SGTC_DIR_ROLE_DEFEND: return "defend";
	case SGTC_DIR_ROLE_ROAM: return "roam";
	case SGTC_DIR_ROLE_ESCORT: return "escort";
	case SGTC_DIR_ROLE_HUNT: return "hunt";
	case SGTC_DIR_ROLE_SUPPORT: return "support";
	default: return "none";
	}
}

// Read an integer that follows `key` inside one line, or -1 when absent.
static int line_int(const char *line, const char *key)
{
	const char *p = strstr(line, key);

	if (p == NULL)
		return -1;
	return atoi(p + strlen(key));
}

// Resolve the current team/class of every kind=bot P line so weapon checks
// use the team/class the bot will actually have when the plan leaves those
// fields at 0 (keep). The document is synthetic and uses the director's real
// P line format.
static void parse_doc_players(const char *doc)
{
	const char *p = doc;

	memset(g_doc_bots, 0, sizeof(g_doc_bots));

	while (p != NULL && *p != '\0')
	{
		const char *eol = strchr(p, '\n');
		char line[512];
		int len = eol ? (int)(eol - p) : (int)strlen(p);

		if (len > (int)sizeof(line) - 1)
			len = (int)sizeof(line) - 1;
		memcpy(line, p, (size_t)len);
		line[len] = '\0';

		if (line[0] == 'P' && line[1] >= '0' && line[1] <= '9'
			&& strstr(line, "kind=bot") != NULL)
		{
			int slot = line_int(line, "slot=");
			int team = line_int(line, "team=");
			int cls = line_int(line, "class=");

			if (slot >= 1 && slot <= SGTC_DIR_MAX_BOTS)
			{
				g_doc_bots[slot].used = 1;
				g_doc_bots[slot].team = team;
				g_doc_bots[slot].cls = cls;
			}
		}

		if (eol == NULL)
			break;
		p = eol + 1;
	}
}

static int eff_team_of(const sgtc_dir_bot_plan_t *b)
{
	if (b->team == 1 || b->team == 2)
		return b->team;
	if (b->bot >= 1 && b->bot <= SGTC_DIR_MAX_BOTS && g_doc_bots[b->bot].used)
		return g_doc_bots[b->bot].team;
	return 0;
}

static int eff_class_of(const sgtc_dir_bot_plan_t *b)
{
	if (b->cls >= 1 && b->cls <= 5)
		return b->cls;
	if (b->bot >= 1 && b->bot <= SGTC_DIR_MAX_BOTS && g_doc_bots[b->bot].used)
		return g_doc_bots[b->bot].cls;
	return 0;
}

static void dump_content(const char *path, const char *content, int len)
{
	FILE *f;

	if (path == NULL || len <= 0)
		return;

	f = fopen(path, "wb");
	if (f == NULL)
	{
		fprintf(stderr, "director_probe: cannot write %s\n", path);
		return;
	}
	if (fwrite(content, 1, (size_t)len, f) != (size_t)len)
		fprintf(stderr, "director_probe: short write to %s\n", path);
	fclose(f);
}

static int setup_fail(const char *message)
{
	printf("{\"status\":-1,\"error\":");
	json_string(message);
	printf("}\n");
	fflush(stdout);
	fprintf(stderr, "director_probe: %s\n", message);
	return 1;
}

int main(int argc, char **argv)
{
	const char *url = NULL;
	const char *model = NULL;
	const char *doc_path = NULL;
	const char *extra = NULL;
	const char *dump_path = NULL;
	int max_tokens = 1500;
	int temperature_x100 = 35;
	int timeout_ms = 15000;

	static char doc[PROBE_DOC_MAX];
	static char body[PROBE_BODY_MAX];
	static char content[PROBE_RESP_MAX];
	char finish_raw[64];
	char parse_err[SGTC_DIR_ERR_MAX];
	sgtc_dir_plan_t plan;
	FILE *f;
	size_t doc_len;
	int body_len;
	int finish_rc = -1;
	int content_len = -1;
	int parse_rc = -1;
	int raw_team_set = 0;
	int team_cleared = 0;
	int team_switches = 0;
	int raw_bots = 0;
	int kept = 0;
	int i;
	double t0;
	double elapsed;

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--url") == 0 && i + 1 < argc)
			url = argv[++i];
		else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc)
			model = argv[++i];
		else if (strcmp(argv[i], "--doc") == 0 && i + 1 < argc)
			doc_path = argv[++i];
		else if (strcmp(argv[i], "--max-tokens") == 0 && i + 1 < argc)
			max_tokens = atoi(argv[++i]);
		else if (strcmp(argv[i], "--temperature-x100") == 0 && i + 1 < argc)
			temperature_x100 = atoi(argv[++i]);
		else if (strcmp(argv[i], "--extra") == 0 && i + 1 < argc)
			extra = argv[++i];
		else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc)
			timeout_ms = atoi(argv[++i]);
		else if (strcmp(argv[i], "--dump-content") == 0 && i + 1 < argc)
			dump_path = argv[++i];
		else
		{
			fprintf(stderr, "director_probe: unknown argument %s\n", argv[i]);
			return setup_fail("bad arguments");
		}
	}

	if (url == NULL || model == NULL || doc_path == NULL)
	{
		fprintf(stderr, "usage: director_probe --url URL --model NAME "
			"--doc FILE [--max-tokens N] [--temperature-x100 35] "
			"[--extra FRAGMENT] [--timeout-ms N] [--dump-content FILE]\n");
		return setup_fail("missing --url, --model or --doc");
	}

	f = fopen(doc_path, "rb");
	if (f == NULL)
		return setup_fail("cannot open the document file");
	doc_len = fread(doc, 1, sizeof(doc) - 1, f);
	if (ferror(f))
	{
		fclose(f);
		return setup_fail("cannot read the document file");
	}
	if (doc_len == sizeof(doc) - 1 && fgetc(f) != EOF)
	{
		fclose(f);
		return setup_fail("document file is too large");
	}
	fclose(f);
	doc[doc_len] = '\0';

	body_len = SGTC_Dir_BuildRequestJson(model, SGTC_Dir_SystemPrompt(), doc,
		max_tokens, temperature_x100, extra, body, (int)sizeof(body));
	if (body_len <= 0)
		return setup_fail("request body does not fit");

	parse_doc_players(doc);

	if (!SGTC_Dir_HttpInit())
		return setup_fail("no HTTP backend available");

	memset(&g_res, 0, sizeof(g_res));
	memset(&plan, 0, sizeof(plan));
	parse_err[0] = '\0';
	finish_raw[0] = '\0';

	t0 = now_ms();

	if (SGTC_Dir_HttpPost(url, getenv("SGTC_BOTDIRECTOR_KEY"), body, body_len,
		timeout_ms, probe_done, &g_res) != 0)
	{
		SGTC_Dir_HttpShutdown();
		return setup_fail("POST was not accepted");
	}

	while (g_res.calls == 0)
	{
		if (now_ms() - t0 > (double)PROBE_PUMP_LIMIT_MS)
			break;
		SGTC_Dir_HttpPoll();
		if (g_res.calls > 0)
			break;
		sleep_ms(10);
	}

	elapsed = now_ms() - t0;
	SGTC_Dir_HttpShutdown();

	if (g_res.calls == 0)
	{
		// The caller caps the whole run; a stuck endpoint is reported exactly
		// as the timeout status so the driver can tell it from a setup error.
		printf("{\"status\":%d}\n", SGTC_DIR_HTTP_TIMEOUT);
		fflush(stdout);
		return 0;
	}

	if (g_res.status == SGTC_DIR_HTTP_OK)
	{
		finish_rc = SGTC_Dir_OpenAIFinishReason(g_res.body, g_res.body_len,
			finish_raw, (int)sizeof(finish_raw));
		if (finish_rc < 0)
			finish_rc = -1;

		content_len = SGTC_Dir_OpenAIExtractContent(g_res.body, g_res.body_len,
			content, (int)sizeof(content));

		if (dump_path != NULL && content_len > 0)
			dump_content(dump_path, content, content_len);

		if (content_len > 0)
		{
			parse_rc = SGTC_Dir_ParsePlan(content, content_len, &plan,
				parse_err, (int)sizeof(parse_err));

			if (parse_rc != SGTC_DIR_PARSE_OK)
			{
				// The caller discards a plan that did not parse; report the
				// same clean state instead of half-filled fields.
				memset(&plan, 0, sizeof(plan));
			}
			else
			{
				raw_bots = plan.bot_count;

				for (i = 0; i < plan.bot_count && i < SGTC_DIR_MAX_BOTS; i++)
				{
					if (plan.bots[i].team != 0)
						raw_team_set++;
				}

				kept = SGTC_Dir_PruneNoOpBots(&plan);
				team_cleared = SGTC_Dir_EnforceTeamRules(&plan, 1, 2);
				(void)kept;

				for (i = 0; i < plan.bot_count && i < SGTC_DIR_MAX_BOTS; i++)
				{
					if (plan.bots[i].team != 0)
						team_switches++;
				}
			}
		}
	}

	printf("{\"status\":%d,\"body_bytes\":%d,\"elapsed_ms\":%lld,"
		"\"finish\":%d,\"finish_reason\":",
		g_res.status, g_res.body_len, (long long)(elapsed + 0.5), finish_rc);
	json_string(finish_raw);

	printf(",\"error\":");
	json_string(g_res.error);

	printf(",\"content_len\":%d,\"parse_rc\":%d,\"parse_err\":",
		content_len, parse_rc);
	json_string(parse_err);

	printf(",\"bot_count\":%d,\"kept_bots\":%d,\"raw_team_set\":%d,"
		"\"team_switches\":%d,\"team_cleared\":%d,"
		"\"pacing\":%d,\"add_bots\":%d,\"kick_bots\":%d,\"usable\":%d,"
		"\"rationale\":",
		raw_bots, kept, raw_team_set, team_switches, team_cleared,
		plan.pacing, plan.add_bots, plan.kick_bots,
		SGTC_Dir_PlanIsUsable(&plan));
	json_string(plan.rationale);

	printf(",\"bots\":[");
	for (i = 0; i < plan.bot_count && i < SGTC_DIR_MAX_BOTS; i++)
	{
		const sgtc_dir_bot_plan_t *b = &plan.bots[i];
		int t = eff_team_of(b);
		int c = eff_class_of(b);
		int w;
		int first = 1;

		if (i > 0)
			putchar(',');

		printf("{\"bot\":%d,\"team\":%d,\"team_eff\":%d,"
			"\"class\":%d,\"class_eff\":%d,\"role\":",
			b->bot, b->team, t, b->cls, c);
		json_string(role_name(b->role));

		printf(",\"skill\":%d,\"aggr\":%.2f,\"acc\":%.2f,\"weapons\":[",
			b->skill, (double)b->aggression_x100 / 100.0,
			(double)b->accuracy_x100 / 100.0);

		for (w = 0; w < SGTC_DIR_WEAPONS_PER_BOT; w++)
		{
			if (b->weapons[w][0] == '\0')
				continue;
			if (!first)
				putchar(',');
			first = 0;
			json_string(b->weapons[w]);
		}

		printf("],\"weapons_ok\":[");
		first = 1;
		for (w = 0; w < SGTC_DIR_WEAPONS_PER_BOT; w++)
		{
			int slot;
			int ok;

			if (b->weapons[w][0] == '\0')
				continue;
			slot = SGTC_Dir_WeaponSlotForName(b->weapons[w]);
			// 1 = grantable, 0 = known but blocked for the effective
			// team/class, -1 = not a weapon at all (or an unknown team/class,
			// which only happens for a slot id the roster does not have).
			if (slot < 0 || t < 1 || c < 1)
				ok = -1;
			else
				ok = SGTC_Dir_WeaponAllowed(t, c, slot) == 1 ? 1 : 0;
			if (!first)
				putchar(',');
			first = 0;
			printf("%d", ok);
		}

		printf("],\"chat\":[");
		first = 1;
		for (w = 0; w < SGTC_DIR_CHAT_LINES_PER_BOT; w++)
		{
			if (b->chat[w][0] == '\0')
				continue;
			if (!first)
				putchar(',');
			first = 0;
			json_string(b->chat[w]);
		}
		printf("]}");
	}

	{
		int t1_attack = 0;
		int t1_defend = 0;
		int t2_attack = 0;
		int t2_defend = 0;

		for (i = 0; i < plan.bot_count && i < SGTC_DIR_MAX_BOTS; i++)
		{
			int t = eff_team_of(&plan.bots[i]);
			int r = plan.bots[i].role;

			if (t == 1 && r == SGTC_DIR_ROLE_ATTACK)
				t1_attack++;
			else if (t == 1 && r == SGTC_DIR_ROLE_DEFEND)
				t1_defend++;
			else if (t == 2 && r == SGTC_DIR_ROLE_ATTACK)
				t2_attack++;
			else if (t == 2 && r == SGTC_DIR_ROLE_DEFEND)
				t2_defend++;
		}

		printf("],\"teams_count\":{\"t1_attack\":%d,\"t1_defend\":%d,"
			"\"t2_attack\":%d,\"t2_defend\":%d}", t1_attack, t1_defend,
			t2_attack, t2_defend);
	}

	printf("}\n");
	fflush(stdout);
	return 0;
}
