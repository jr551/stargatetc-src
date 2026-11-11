// tests/host/director_http_harness.cpp - host harness for dlls/sgtc_director_http.cpp.
//
// Drives the real client against tests/director_mock_server.py on the local
// host. URLs are passed as argv so the Python driver owns the mock port:
//
//   director_http_harness ECHO_URL SLOW_URL MALFORMED_URL BADJSON_URL \
//                         ERROR500_URL BIG_URL HANG_URL
//
// Every check prints one line the driver parses:
//   CASE <name> PASS|FAIL <detail>
// and the process exits nonzero when any CASE failed. The harness never logs
// the bearer token it sends; the mock only reports presence.
//
// Besides the per-URL probes it covers the slow endpoint timing out, a
// never-answering endpoint (HANG_URL) timing out and freeing the single
// in-flight slot for the next request, the reply-size cap, Post-while-busy and
// shutdown with a request in flight.
//
// Build (the test script does this):
//   c++ -std=c++11 -O1 -g -pthread -Idlls -o harness \
//       dlls/sgtc_director_http.cpp tests/host/director_http_harness.cpp

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "sgtc_director_http.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#define HARNESS_BODY_MAX 70000

struct Result
{
	int calls;
	int status;
	int len;
	char body[HARNESS_BODY_MAX];
	char error[300];
};

static Result g_res;
static int g_failures;

struct ChainCtx
{
	Result first;
	Result second;
	const char *url;
};

static ChainCtx g_chain;

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

static void report(const char *name, int ok, const char *fmt, ...)
{
	char detail[800];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(detail, sizeof(detail), fmt, ap);
	va_end(ap);
	printf("CASE %s %s %s\n", name, ok ? "PASS" : "FAIL", detail);
	fflush(stdout);
	if (!ok)
		g_failures++;
}

static void reset_result(Result *r)
{
	memset(r, 0, sizeof(*r));
}

static void on_done(void *ctx, int status, const char *body, int body_len,
	const char *error)
{
	Result *r = (Result *)ctx;
	int n;

	if (r == NULL)
		return;

	r->calls++;
	r->status = status;
	r->len = body_len;
	n = body_len;
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

// Poll until the callback ran or the budget expired.
static int pump(Result *r, int limit_ms)
{
	int waited = 0;

	while (waited < limit_ms)
	{
		SGTC_Dir_HttpPoll();
		if (r->calls > 0)
			return 1;
		sleep_ms(5);
		waited += 5;
	}
	SGTC_Dir_HttpPoll();
	return r->calls > 0;
}

static int body_has(const Result *r, const char *needle)
{
	return strstr(r->body, needle) != NULL;
}

// Posts the second request from inside the first completion callback; this is
// the pattern the director uses to chain a follow-up without waiting a frame.
static void on_done_chain(void *ctx, int status, const char *body, int body_len,
	const char *error)
{
	ChainCtx *c = (ChainCtx *)ctx;

	on_done(&c->first, status, body, body_len, error);
	if (c->first.calls == 1 && status == SGTC_DIR_HTTP_OK
		&& c->second.calls == 0)
	{
		SGTC_Dir_HttpPost(c->url, NULL, "{\"second\":true}", 15, 5000,
			on_done, &c->second);
	}
}

// A finished request must not fire a second callback and Poll must then idle.
static int settled_once(Result *r)
{
	sleep_ms(40);
	if (SGTC_Dir_HttpPoll() != 0)
		return 0;
	sleep_ms(40);
	if (SGTC_Dir_HttpPoll() != 0)
		return 0;
	return r->calls == 1;
}

int main(int argc, char **argv)
{
	const char *echo_url;
	const char *slow_url;
	const char *malformed_url;
	const char *badjson_url;
	const char *error_url;
	const char *big_url;
	const char *hang_url;
	static char oversized[32769 + 1];
	double started;
	double elapsed;
	int rc;
	int got;

	if (argc < 8)
	{
		fprintf(stderr,
			"usage: %s ECHO_URL SLOW_URL MALFORMED_URL BADJSON_URL "
			"ERROR500_URL BIG_URL HANG_URL\n", argv[0]);
		return 2;
	}
	echo_url = argv[1];
	slow_url = argv[2];
	malformed_url = argv[3];
	badjson_url = argv[4];
	error_url = argv[5];
	big_url = argv[6];
	hang_url = argv[7];

	rc = SGTC_Dir_HttpInit();
	report("init", rc == 1 && SGTC_Dir_HttpAvailable() == 1
			&& strcmp(SGTC_Dir_HttpBackend(), "none") != 0,
		"init=%d available=%d backend=%s", rc, SGTC_Dir_HttpAvailable(),
		SGTC_Dir_HttpBackend());
	if (rc != 1)
	{
		printf("SUMMARY FAIL init failed\n");
		return 1;
	}

	report("poll-idle", SGTC_Dir_HttpPoll() == 0,
		"Poll with no request returns 0");

	// Body hard cap: 32769 bytes must be rejected before the worker wakes.
	memset(oversized, 'a', sizeof(oversized) - 1);
	oversized[sizeof(oversized) - 1] = '\0';
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost(echo_url, NULL, oversized, 32769, 2000, on_done,
		&g_res);
	report("post-body-cap", rc == -1 && g_res.calls == 0
			&& SGTC_Dir_HttpBusy() == 0,
		"32769-byte body -> %d calls=%d busy=%d", rc, g_res.calls,
		SGTC_Dir_HttpBusy());

	// Scheme whitelist.
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost("ftp://127.0.0.1/nope", NULL, "{}", 2, 2000,
		on_done, &g_res);
	report("post-scheme", rc == -1 && g_res.calls == 0,
		"ftp:// URL -> %d calls=%d", rc, g_res.calls);

	// A refused connection is CONNECT, not TIMEOUT or BAD_STATUS.
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost("http://127.0.0.1:0/down", NULL, "{}", 2, 2000,
		on_done, &g_res);
	got = (rc == 0) ? pump(&g_res, 5000) : 0;
	report("connect-refused", rc == 0 && got
			&& g_res.status == SGTC_DIR_HTTP_CONNECT
			&& g_res.error[0] != '\0',
		"rc=%d calls=%d status=%d err=%s", rc, g_res.calls, g_res.status,
		g_res.error);
	settled_once(&g_res);

	// Successful POST: body echoed, bearer header seen by the mock.
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost(echo_url, "sk-harness-token", "{\"hello\":\"world\"}",
		17, 5000, on_done, &g_res);
	got = 0;
	if (rc == 0)
	{
		if (SGTC_Dir_HttpBusy() != 1)
			report("post-busy-flag", 0, "busy=%d right after Post",
				SGTC_Dir_HttpBusy());
		else
			report("post-busy-flag", 1, "busy=1 right after Post");
		got = pump(&g_res, 6000);
	}
	else
	{
		report("post-busy-flag", 0, "Post rejected rc=%d", rc);
	}
	report("post-echo", rc == 0 && got && g_res.status == SGTC_DIR_HTTP_OK
			&& body_has(&g_res, "\"hello\":\"world\"")
			&& body_has(&g_res, "\"auth\":\"bearer\""),
		"rc=%d calls=%d status=%d len=%d err=%s", rc, g_res.calls,
		g_res.status, g_res.len, g_res.error);
	report("post-echo-once", settled_once(&g_res),
		"calls=%d after extra Polls", g_res.calls);

	// Keyless POST: no Authorization header must be sent.
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost(echo_url, NULL, "{\"keyless\":true}", 16, 5000,
		on_done, &g_res);
	got = (rc == 0) ? pump(&g_res, 6000) : 0;
	report("post-no-key", rc == 0 && got && g_res.status == SGTC_DIR_HTTP_OK
			&& body_has(&g_res, "\"auth\":\"none\"")
			&& body_has(&g_res, "\"keyless\":true"),
		"rc=%d calls=%d status=%d len=%d", rc, g_res.calls, g_res.status,
		g_res.len);
	report("post-no-key-once", settled_once(&g_res),
		"calls=%d after extra Polls", g_res.calls);

	// A completion callback may Post the next request immediately.
	memset(&g_chain, 0, sizeof(g_chain));
	g_chain.url = echo_url;
	rc = SGTC_Dir_HttpPost(echo_url, NULL, "{\"first\":true}", 14, 5000,
		on_done_chain, &g_chain);
	got = (rc == 0) ? pump(&g_chain.second, 8000) : 0;
	report("post-from-callback", rc == 0 && got
			&& g_chain.first.calls == 1 && g_chain.second.calls == 1
			&& g_chain.first.status == SGTC_DIR_HTTP_OK
			&& g_chain.second.status == SGTC_DIR_HTTP_OK
			&& body_has(&g_chain.first, "\"first\":true")
			&& body_has(&g_chain.second, "\"second\":true"),
		"rc=%d first=%d/%d second=%d/%d", rc, g_chain.first.calls,
		g_chain.first.status, g_chain.second.calls, g_chain.second.status);

	// A body near the 32 KiB cap must be transmitted intact.
	{
		static char large_req[20011];
		int prefix = snprintf(large_req, sizeof(large_req), "{\"pad\":\"");
		int large_len;

		memset(large_req + prefix, 'y', 20000);
		large_req[prefix + 20000] = '"';
		large_req[prefix + 20001] = '}';
		large_len = prefix + 20002;

		reset_result(&g_res);
		rc = SGTC_Dir_HttpPost(echo_url, NULL, large_req, large_len, 5000,
			on_done, &g_res);
		got = (rc == 0) ? pump(&g_res, 8000) : 0;
		report("post-large-body", rc == 0 && got
				&& g_res.status == SGTC_DIR_HTTP_OK
				&& g_res.len > 20000 && g_res.len <= 65536
				&& body_has(&g_res, "\"pad\":\"yyyy")
				&& body_has(&g_res, "\"auth\":\"none\""),
			"rc=%d calls=%d status=%d len=%d", rc, g_res.calls,
			g_res.status, g_res.len);
		settled_once(&g_res);
	}

	// Non-JSON prose is still a transport success; parsing is the core's job.
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost(malformed_url, NULL, "{}", 2, 5000, on_done,
		&g_res);
	got = (rc == 0) ? pump(&g_res, 6000) : 0;
	report("malformed-passthrough", rc == 0 && got
			&& g_res.status == SGTC_DIR_HTTP_OK
			&& body_has(&g_res, "plain prose"),
		"rc=%d calls=%d status=%d len=%d", rc, g_res.calls, g_res.status,
		g_res.len);
	settled_once(&g_res);

	// Broken JSON with a 200 is delivered verbatim, not rejected here.
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost(badjson_url, NULL, "{}", 2, 5000, on_done,
		&g_res);
	got = (rc == 0) ? pump(&g_res, 6000) : 0;
	report("badjson-passthrough", rc == 0 && got
			&& g_res.status == SGTC_DIR_HTTP_OK
			&& body_has(&g_res, "{\"bots\":[{\"bot\":1,"),
		"rc=%d calls=%d status=%d len=%d err=%s", rc, g_res.calls,
		g_res.status, g_res.len, g_res.error);
	settled_once(&g_res);

	// HTTP 500 maps to BAD_STATUS with the server's error text preserved.
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost(error_url, NULL, "{}", 2, 5000, on_done,
		&g_res);
	got = (rc == 0) ? pump(&g_res, 6000) : 0;
	report("http-500", rc == 0 && got
			&& g_res.status == SGTC_DIR_HTTP_BAD_STATUS
			&& strstr(g_res.error, "500") != NULL
			&& body_has(&g_res, "boom"),
		"rc=%d calls=%d status=%d len=%d err=%s", rc, g_res.calls,
		g_res.status, g_res.len, g_res.error);
	settled_once(&g_res);

	// timeout_ms=800 is used as given (the floor is 100 ms); the mock sleeps 10 s.
	reset_result(&g_res);
	started = now_ms();
	rc = SGTC_Dir_HttpPost(slow_url, NULL, "{}", 2, 800, on_done, &g_res);
	got = (rc == 0) ? pump(&g_res, 6000) : 0;
	elapsed = now_ms() - started;
	report("slow-timeout", rc == 0 && got
			&& g_res.status == SGTC_DIR_HTTP_TIMEOUT,
		"rc=%d calls=%d status=%d elapsed=%.0fms err=%s", rc, g_res.calls,
		g_res.status, elapsed, g_res.error);
	report("slow-timeout-wallclock", elapsed >= 500.0 && elapsed <= 2500.0,
		"elapsed=%.0fms (want ~800ms)", elapsed);
	settled_once(&g_res);

	// A server that accepts the request and never answers must still end the
	// exchange on the client timeout, which frees the single in-flight slot so
	// the next request is accepted.
	reset_result(&g_res);
	started = now_ms();
	rc = SGTC_Dir_HttpPost(hang_url, NULL, "{}", 2, 800, on_done, &g_res);
	sleep_ms(150);
	if (rc == 0)
	{
		int held = SGTC_Dir_HttpBusy();
		int second = SGTC_Dir_HttpPost(echo_url, NULL, "{}", 2, 2000,
			on_done, &g_res);

		report("hang-while-busy", held == 1 && second == -1,
			"busy=%d second Post -> %d (want 1/-1)", held, second);
	}
	else
	{
		report("hang-while-busy", 0, "hang Post rejected rc=%d", rc);
	}
	got = (rc == 0) ? pump(&g_res, 6000) : 0;
	elapsed = now_ms() - started;
	report("hang-timeout", rc == 0 && got && g_res.calls == 1
			&& g_res.status == SGTC_DIR_HTTP_TIMEOUT,
		"rc=%d calls=%d status=%d elapsed=%.0fms err=%s", rc, g_res.calls,
		g_res.status, elapsed, g_res.error);
	report("hang-timeout-wallclock", elapsed >= 500.0 && elapsed <= 2500.0,
		"elapsed=%.0fms (want ~800ms)", elapsed);
	report("hang-empty-body", g_res.calls == 1 && g_res.len == 0,
		"len=%d first=%d", g_res.len, (int)(unsigned char)g_res.body[0]);
	report("hang-slot-freed", SGTC_Dir_HttpBusy() == 0,
		"busy=%d after the timeout callback", SGTC_Dir_HttpBusy());
	settled_once(&g_res);

	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost(echo_url, NULL, "{\"after\":true}", 14, 5000,
		on_done, &g_res);
	got = (rc == 0) ? pump(&g_res, 6000) : 0;
	report("hang-next-request", rc == 0 && got && g_res.calls == 1
			&& g_res.status == SGTC_DIR_HTTP_OK
			&& body_has(&g_res, "\"after\":true"),
		"rc=%d calls=%d status=%d len=%d", rc, g_res.calls, g_res.status,
		g_res.len);
	settled_once(&g_res);

	// A reply over the 64 KiB cap is truncated and reported as TOO_BIG.
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost(big_url, NULL, "{}", 2, 8000, on_done, &g_res);
	got = (rc == 0) ? pump(&g_res, 9000) : 0;
	report("oversized-reply", rc == 0 && got
			&& g_res.status == SGTC_DIR_HTTP_TOO_BIG
			&& g_res.len > 0 && g_res.len <= 65536 && g_res.error[0] != '\0',
		"rc=%d calls=%d status=%d len=%d err=%s", rc, g_res.calls,
		g_res.status, g_res.len, g_res.error);
	settled_once(&g_res);

	// Second Post while the first is in flight must fail fast and must not
	// produce a second callback.
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost(slow_url, NULL, "{}", 2, 1500, on_done, &g_res);
	got = 0;
	if (rc == 0)
	{
		int second = SGTC_Dir_HttpPost(echo_url, NULL, "{}", 2, 2000,
			on_done, &g_res);

		report("post-while-busy", second == -1,
			"second Post -> %d (want -1)", second);
		got = pump(&g_res, 6000);
	}
	else
	{
		report("post-while-busy", 0, "first Post rejected rc=%d", rc);
	}
	report("busy-result", rc == 0 && got && g_res.calls == 1
			&& g_res.status == SGTC_DIR_HTTP_TIMEOUT,
		"rc=%d calls=%d status=%d", rc, g_res.calls, g_res.status);
	settled_once(&g_res);

	// Shutdown with a request in flight: one CANCELLED callback, then the
	// backend is gone and further calls fail cleanly.
	reset_result(&g_res);
	rc = SGTC_Dir_HttpPost(slow_url, NULL, "{}", 2, 20000, on_done, &g_res);
	sleep_ms(150);
	started = now_ms();
	SGTC_Dir_HttpShutdown();
	elapsed = now_ms() - started;
	report("shutdown-cancel", rc == 0 && g_res.calls == 1
			&& g_res.status == SGTC_DIR_HTTP_CANCELLED,
		"rc=%d calls=%d status=%d elapsed=%.0fms err=%s", rc, g_res.calls,
		g_res.status, elapsed, g_res.error);
	report("shutdown-cancel-prompt", elapsed < 5000.0,
		"Shutdown returned in %.0fms", elapsed);
	sleep_ms(100);
	rc = SGTC_Dir_HttpPost(echo_url, NULL, "{}", 2, 1000, on_done, &g_res);
	report("shutdown-teardown", SGTC_Dir_HttpPoll() == 0 && rc == -1
			&& g_res.calls == 1 && SGTC_Dir_HttpAvailable() == 0
			&& strcmp(SGTC_Dir_HttpBackend(), "none") == 0,
		"poll=%d post=%d calls=%d available=%d backend=%s",
		SGTC_Dir_HttpPoll(), rc, g_res.calls, SGTC_Dir_HttpAvailable(),
		SGTC_Dir_HttpBackend());

	// Init after Shutdown must start a fresh worker (server reload path).
	rc = SGTC_Dir_HttpInit();
	reset_result(&g_res);
	got = 0;
	if (rc == 1)
	{
		int again = SGTC_Dir_HttpPost(echo_url, NULL, "{\"again\":true}", 14,
			5000, on_done, &g_res);

		if (again == 0)
			got = pump(&g_res, 6000);
		else
			rc = -1;
	}
	report("reinit-after-shutdown", rc == 1 && got && g_res.calls == 1
			&& g_res.status == SGTC_DIR_HTTP_OK
			&& body_has(&g_res, "\"again\":true"),
		"init=%d calls=%d status=%d len=%d", rc, g_res.calls, g_res.status,
		g_res.len);
	SGTC_Dir_HttpShutdown();
	report("final-teardown", SGTC_Dir_HttpAvailable() == 0
			&& strcmp(SGTC_Dir_HttpBackend(), "none") == 0
			&& SGTC_Dir_HttpPoll() == 0,
		"available=%d backend=%s", SGTC_Dir_HttpAvailable(),
		SGTC_Dir_HttpBackend());

	if (g_failures != 0)
	{
		printf("SUMMARY FAIL %d failed case(s)\n", g_failures);
		return 1;
	}
	printf("SUMMARY PASS\n");
	return 0;
}
