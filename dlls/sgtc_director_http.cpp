// sgtc_director_http.cpp - non-blocking HTTP POST for the bot director.
//
// Threading contract:
//   * SGTC_Dir_HttpPost/Poll/Shutdown are game-thread calls; only the worker
//     thread and the network stack may block.
//   * one request is in flight at a time. Post copies the URL, key and body
//     into a fixed request slot and wakes the worker; Poll hands the finished
//     result back and runs the callback on the game thread exactly once.
//   * Shutdown joins the worker (libcurl aborts through the progress callback)
//     and delivers exactly one CANCELLED result for anything still pending.
//   * the callback runs after the finished request stops counting as busy, so
//     it may Post the next request directly; do not call Poll recursively from
//     a callback (one callback per Poll is the contract).
//
// Secret handling: the API key is copied into g_req.key, used only to build the
// Authorization header, zeroised as soon as the transfer ends, and never copied
// into the result slot, the error string or a log line. The key never appears
// in an URL built here.
//
// Backends: libcurl through dlopen on Linux/macOS (no link-time dependency),
// WinHTTP on Windows. Both are selected at runtime; a missing library leaves
// SGTC_Dir_HttpAvailable() == 0 and the director running local-only.
//
// Build note: POSIX needs the pthread symbols (part of libc on glibc >= 2.34,
// otherwise link -lpthread); Windows needs winhttp (MSVC via the pragma below,
// MinGW via -lwinhttp). No link-time curl dependency exists on any platform.

#include "sgtc_director_http.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <winhttp.h>

#if defined(_MSC_VER)
#pragma comment(lib, "winhttp.lib")
#endif

#else

#include <dlfcn.h>
#include <pthread.h>

#endif

#define SGTC_HTTP_REQ_MAX (32 * 1024)
#define SGTC_HTTP_RESP_MAX (64 * 1024)
#define SGTC_HTTP_URL_MAX 2048
#define SGTC_HTTP_KEY_MAX 2048
#define SGTC_HTTP_TIMEOUT_DEFAULT 15000
#define SGTC_HTTP_TIMEOUT_MIN 100
#define SGTC_HTTP_TIMEOUT_MAX 60000
#define SGTC_HTTP_ERR_MAX 192

typedef struct sgtc_http_req_s
{
	char url[SGTC_HTTP_URL_MAX];
	char key[SGTC_HTTP_KEY_MAX];
	char body[SGTC_HTTP_REQ_MAX];
	int body_len;
	int timeout_ms;
	sgtc_dir_http_done_fn done;
	void *ctx;
} sgtc_http_req_t;

typedef struct sgtc_http_write_s
{
	char *buf;
	int cap;
	int len;
	int overflow;
} sgtc_http_write_t;

/* --------------------------------------------------------------------- */
/* state                                                                  */
/* --------------------------------------------------------------------- */

static int g_inited = 0;
static int g_available = 0;
static const char *g_backend = "none";

static int g_sync_ready = 0;
static int g_thread_started = 0;
static int g_busy = 0;         // accepted request not yet polled
static int g_have_req = 0;     // queued for the worker
static int g_slot_ready = 0;   // a result waits for Poll/Shutdown
static int g_slot_status = SGTC_DIR_HTTP_CONNECT;
static int g_slot_len = 0;
static char g_slot_body[SGTC_HTTP_RESP_MAX + 1];
static char g_slot_error[SGTC_HTTP_ERR_MAX];
static sgtc_dir_http_done_fn g_slot_done = NULL;
static void *g_slot_ctx = NULL;

// The worker fills g_slot_body while the request is in flight (nothing reads it
// until g_slot_ready is published); Poll copies it here for the callback so a
// callback that Posts the next request cannot race the worker.
static char g_cb_body[SGTC_HTTP_RESP_MAX + 1];

static sgtc_http_req_t g_req;
static int g_quit = 0;

#if defined(_WIN32)

static CRITICAL_SECTION g_lock;
static HANDLE g_wake = NULL;
static HANDLE g_thread = NULL;
static HINTERNET g_winhttp_session = NULL;

static void http_set_quit(int v)
{
	InterlockedExchange((LONG *)&g_quit, v ? 1 : 0);
}

static int http_quit_requested(void)
{
	return InterlockedCompareExchange((LONG *)&g_quit, 0, 0) != 0;
}

static void http_lock(void)
{
	EnterCriticalSection(&g_lock);
}

static void http_unlock(void)
{
	LeaveCriticalSection(&g_lock);
}

static void http_wake(void)
{
	SetEvent(g_wake);
}

static void http_wait(void)
{
	LeaveCriticalSection(&g_lock);
	WaitForSingleObject(g_wake, INFINITE);
	EnterCriticalSection(&g_lock);
}

#else

static pthread_mutex_t g_lock;
static pthread_cond_t g_wake;
static pthread_t g_thread;

static void http_set_quit(int v)
{
#if defined(__GNUC__)
	__atomic_store_n(&g_quit, v ? 1 : 0, __ATOMIC_RELAXED);
#else
	g_quit = v ? 1 : 0;
#endif
}

static int http_quit_requested(void)
{
#if defined(__GNUC__)
	return __atomic_load_n(&g_quit, __ATOMIC_RELAXED) != 0;
#else
	return g_quit != 0;
#endif
}

static void http_lock(void)
{
	pthread_mutex_lock(&g_lock);
}

static void http_unlock(void)
{
	pthread_mutex_unlock(&g_lock);
}

static void http_wake(void)
{
	pthread_cond_signal(&g_wake);
}

static void http_wait(void)
{
	pthread_cond_wait(&g_wake, &g_lock);
}

#endif

/* --------------------------------------------------------------------- */
/* small helpers                                                          */
/* --------------------------------------------------------------------- */

// Compiler must not turn this into a dead store.
static void http_secure_zero(void *ptr, size_t len)
{
	volatile unsigned char *p = (volatile unsigned char *)ptr;
	while (len--)
		*p++ = 0;
}

static void http_copy(char *dst, int cap, const char *src)
{
	int i = 0;

	if (cap <= 0)
		return;
	if (src)
	{
		while (i < cap - 1 && src[i] != '\0')
		{
			dst[i] = src[i];
			i++;
		}
	}
	dst[i] = '\0';
}

static void http_error(char *err, int errcap, const char *fmt, ...)
{
	va_list ap;

	if (err == NULL || errcap <= 0)
		return;
	va_start(ap, fmt);
	vsnprintf(err, (size_t)errcap, fmt, ap);
	va_end(ap);
	err[errcap - 1] = '\0';
}

// Bounded strlen: -1 when there is no NUL inside the first `max` bytes.
static int http_bounded_len(const char *s, int max)
{
	int i;

	if (s == NULL)
		return -1;
	for (i = 0; i < max; i++)
	{
		if (s[i] == '\0')
			return i;
	}
	return -1;
}

static int http_ci_prefix(const char *s, const char *prefix)
{
	while (*prefix != '\0')
	{
		char a = *s++;
		char b = *prefix++;

		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if (a != b)
			return 0;
	}
	return 1;
}

// Only absolute http(s) URLs with a host and no raw control bytes. This is
// deliberately stricter than the URL parsers: the endpoint comes from a cvar
// and must never be able to smuggle a host header or another scheme.
static int http_url_ok(const char *url)
{
	int len;
	int slen;
	int i;

	len = http_bounded_len(url, SGTC_HTTP_URL_MAX);
	if (len <= 0)
		return 0;

	if (http_ci_prefix(url, "https://"))
		slen = 8;
	else if (http_ci_prefix(url, "http://"))
		slen = 7;
	else
		return 0;

	if (len <= slen)
		return 0;
	if (url[slen] == '/' || url[slen] == '?' || url[slen] == '#' || url[slen] == ':')
		return 0;

	for (i = 0; i < len; i++)
	{
		unsigned char c = (unsigned char)url[i];

		if (c <= 0x20 || c == 0x7f)
			return 0;
	}
	return 1;
}

// Bearer tokens are header values: no spaces, no CR/LF, no control bytes.
static int http_key_ok(const char *key)
{
	int len;
	int i;

	len = http_bounded_len(key, SGTC_HTTP_KEY_MAX);
	if (len < 0)
		return 0;
	for (i = 0; i < len; i++)
	{
		unsigned char c = (unsigned char)key[i];

		if (c < 0x21 || c == 0x7f)
			return 0;
	}
	return 1;
}

static int http_timeout_value(int timeout_ms)
{
	if (timeout_ms <= 0)
		return SGTC_HTTP_TIMEOUT_DEFAULT;
	if (timeout_ms < SGTC_HTTP_TIMEOUT_MIN)
		return SGTC_HTTP_TIMEOUT_MIN;
	if (timeout_ms > SGTC_HTTP_TIMEOUT_MAX)
		return SGTC_HTTP_TIMEOUT_MAX;
	return timeout_ms;
}

static void http_body_append(sgtc_http_write_t *w, const char *src, size_t n)
{
	if (n == 0)
		return;
	if (w->len < w->cap)
	{
		size_t room = (size_t)(w->cap - w->len);
		size_t take = n < room ? n : room;

		if (take > 0)
			memcpy(w->buf + w->len, src, take);
		w->len += (int)take;
		if (take < n)
			w->overflow = 1;
	}
	else
	{
		w->overflow = 1;
	}
}

// Common status decision once the transport finished normally.
static int http_classify(const sgtc_http_write_t *w, long code, char *err,
	int errcap)
{
	if (w->overflow)
	{
		http_error(err, errcap, "response bigger than %d bytes",
			SGTC_HTTP_RESP_MAX);
		return SGTC_DIR_HTTP_TOO_BIG;
	}
	if (code == 0)
	{
		http_error(err, errcap, "no HTTP response");
		return SGTC_DIR_HTTP_CONNECT;
	}
	if (code < 200 || code > 299)
	{
		http_error(err, errcap, "HTTP %ld", code);
		return SGTC_DIR_HTTP_BAD_STATUS;
	}
	if (w->len == 0)
	{
		http_error(err, errcap, "empty response body");
		return SGTC_DIR_HTTP_BAD_STATUS;
	}
	return SGTC_DIR_HTTP_OK;
}

/* --------------------------------------------------------------------- */
/* backend: libcurl via dlopen (Linux/macOS)                              */
/* --------------------------------------------------------------------- */

#if !defined(_WIN32)

typedef void *(*sgtc_curl_easy_init_fn)(void);
typedef void (*sgtc_curl_easy_cleanup_fn)(void *);
typedef int (*sgtc_curl_easy_setopt_fn)(void *, int, ...);
typedef int (*sgtc_curl_easy_perform_fn)(void *);
typedef int (*sgtc_curl_easy_getinfo_fn)(void *, int, ...);
typedef const char *(*sgtc_curl_easy_strerror_fn)(int);
typedef void *(*sgtc_curl_slist_append_fn)(void *, const char *);
typedef void (*sgtc_curl_slist_free_all_fn)(void *);
typedef int (*sgtc_curl_global_init_fn)(long);
typedef void (*sgtc_curl_global_cleanup_fn)(void);

// Public libcurl ABI values (curl/curl.h). Only the stable option numbers we
// need are declared here; including curl headers would add a build dependency
// the game DLL deliberately does not have.
enum
{
	SGTC_CURLOPT_POST = 47,
	SGTC_CURLOPT_FOLLOWLOCATION = 52,
	SGTC_CURLOPT_POSTFIELDSIZE = 60,
	SGTC_CURLOPT_NOSIGNAL = 99,
	SGTC_CURLOPT_TIMEOUT_MS = 155,
	SGTC_CURLOPT_CONNECTTIMEOUT_MS = 156,
	SGTC_CURLOPT_PROTOCOLS = 181,
	SGTC_CURLOPT_REDIR_PROTOCOLS = 182,
	SGTC_CURLOPT_NOPROGRESS = 43,
	SGTC_CURLOPT_WRITEDATA = 10001,
	SGTC_CURLOPT_URL = 10002,
	SGTC_CURLOPT_POSTFIELDS = 10015,
	SGTC_CURLOPT_USERAGENT = 10018,
	SGTC_CURLOPT_HTTPHEADER = 10023,
	SGTC_CURLOPT_XFERINFODATA = 10057,
	SGTC_CURLOPT_WRITEFUNCTION = 20011,
	SGTC_CURLOPT_XFERINFOFUNCTION = 20219,
	SGTC_CURLINFO_RESPONSE_CODE = 0x200002,
	SGTC_CURLE_OK = 0,
	SGTC_CURLE_OPERATION_TIMEDOUT = 28,
	SGTC_CURLE_ABORTED_BY_CALLBACK = 42,
	SGTC_CURLPROTO_HTTP_HTTPS = 3,
	SGTC_CURL_GLOBAL_DEFAULT = 3
};

typedef struct sgtc_curl_api_s
{
	void *lib;
	sgtc_curl_easy_init_fn easy_init;
	sgtc_curl_easy_cleanup_fn easy_cleanup;
	sgtc_curl_easy_setopt_fn easy_setopt;
	sgtc_curl_easy_perform_fn easy_perform;
	sgtc_curl_easy_getinfo_fn easy_getinfo;
	sgtc_curl_easy_strerror_fn easy_strerror;
	sgtc_curl_slist_append_fn slist_append;
	sgtc_curl_slist_free_all_fn slist_free_all;
	sgtc_curl_global_init_fn global_init;
	sgtc_curl_global_cleanup_fn global_cleanup;
} sgtc_curl_api_t;

static sgtc_curl_api_t g_curl;

static int http_curl_bind(void *lib)
{
	memset(&g_curl, 0, sizeof(g_curl));
	g_curl.lib = lib;
	g_curl.easy_init = (sgtc_curl_easy_init_fn)dlsym(lib, "curl_easy_init");
	g_curl.easy_cleanup = (sgtc_curl_easy_cleanup_fn)dlsym(lib,
		"curl_easy_cleanup");
	g_curl.easy_setopt = (sgtc_curl_easy_setopt_fn)dlsym(lib,
		"curl_easy_setopt");
	g_curl.easy_perform = (sgtc_curl_easy_perform_fn)dlsym(lib,
		"curl_easy_perform");
	g_curl.easy_getinfo = (sgtc_curl_easy_getinfo_fn)dlsym(lib,
		"curl_easy_getinfo");
	g_curl.easy_strerror = (sgtc_curl_easy_strerror_fn)dlsym(lib,
		"curl_easy_strerror");
	g_curl.slist_append = (sgtc_curl_slist_append_fn)dlsym(lib,
		"curl_slist_append");
	g_curl.slist_free_all = (sgtc_curl_slist_free_all_fn)dlsym(lib,
		"curl_slist_free_all");

	if (g_curl.easy_init == NULL || g_curl.easy_cleanup == NULL
		|| g_curl.easy_setopt == NULL || g_curl.easy_perform == NULL
		|| g_curl.easy_getinfo == NULL || g_curl.easy_strerror == NULL
		|| g_curl.slist_append == NULL || g_curl.slist_free_all == NULL)
	{
		memset(&g_curl, 0, sizeof(g_curl));
		return 0;
	}

	g_curl.global_init = (sgtc_curl_global_init_fn)dlsym(lib,
		"curl_global_init");
	g_curl.global_cleanup = (sgtc_curl_global_cleanup_fn)dlsym(lib,
		"curl_global_cleanup");
	if (g_curl.global_init == NULL || g_curl.global_cleanup == NULL)
	{
		g_curl.global_init = NULL;
		g_curl.global_cleanup = NULL;
	}
	return 1;
}

static size_t http_curl_write(char *ptr, size_t size, size_t nmemb,
	void *userdata)
{
	sgtc_http_write_t *w = (sgtc_http_write_t *)userdata;
	size_t n = size * nmemb;

	http_body_append(w, ptr, n);
	return n;  // consume everything; the cap is enforced by truncation
}

// Abort an in-flight transfer promptly when Shutdown is called. libcurl calls
// this from its own loop, so Shutdown does not have to wait for the whole
// timeout against an endpoint that stopped answering.
static int http_curl_progress(void *, long long, long long, long long,
	long long)
{
	return http_quit_requested() ? 1 : 0;
}

static int http_curl_slist_add(void **list, const char *value)
{
	void *next = g_curl.slist_append(*list, value);

	if (next == NULL)
	{
		if (*list != NULL)
			g_curl.slist_free_all(*list);
		*list = NULL;
		return 0;
	}
	*list = next;
	return 1;
}

static int http_curl_post(const sgtc_http_req_t *req, char *out, int cap,
	int *out_len, char *err, int errcap)
{
	void *easy;
	void *slist = NULL;
	char auth[SGTC_HTTP_KEY_MAX + 40];
	long code = 0;
	int headers_ok = 1;
	int rc;
	int status;
	sgtc_http_write_t w;

	easy = g_curl.easy_init();
	if (easy == NULL)
	{
		http_error(err, errcap, "curl_easy_init failed");
		*out_len = 0;
		out[0] = '\0';
		return SGTC_DIR_HTTP_CONNECT;
	}

	w.buf = out;
	w.cap = cap;
	w.len = 0;
	w.overflow = 0;

	g_curl.easy_setopt(easy, SGTC_CURLOPT_URL, req->url);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_POST, 1L);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_POSTFIELDS, req->body);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_POSTFIELDSIZE, (long)req->body_len);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_WRITEFUNCTION, http_curl_write);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_WRITEDATA, &w);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_NOSIGNAL, 1L);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_USERAGENT, "SGTC-BotDirector/1.0");
	g_curl.easy_setopt(easy, SGTC_CURLOPT_FOLLOWLOCATION, 0L);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_CONNECTTIMEOUT_MS,
		(long)req->timeout_ms);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_TIMEOUT_MS, (long)req->timeout_ms);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_PROTOCOLS,
		(long)SGTC_CURLPROTO_HTTP_HTTPS);
	g_curl.easy_setopt(easy, SGTC_CURLOPT_REDIR_PROTOCOLS,
		(long)SGTC_CURLPROTO_HTTP_HTTPS);

	// Enable the progress callback only when libcurl accepted it; an old
	// libcurl would otherwise print its default progress meter to stderr.
	if (g_curl.easy_setopt(easy, SGTC_CURLOPT_XFERINFOFUNCTION,
			http_curl_progress) == SGTC_CURLE_OK)
	{
		g_curl.easy_setopt(easy, SGTC_CURLOPT_XFERINFODATA, (void *)NULL);
		g_curl.easy_setopt(easy, SGTC_CURLOPT_NOPROGRESS, 0L);
	}

	headers_ok &= http_curl_slist_add(&slist,
		"Content-Type: application/json");
	headers_ok &= http_curl_slist_add(&slist, "Accept: application/json");
	headers_ok &= http_curl_slist_add(&slist, "Expect:");
	if (req->key[0] != '\0')
	{
		http_error(auth, (int)sizeof(auth), "Authorization: Bearer %s",
			req->key);
		headers_ok &= http_curl_slist_add(&slist, auth);
		http_secure_zero(auth, sizeof(auth));
	}
	if (!headers_ok)
	{
		if (slist != NULL)
			g_curl.slist_free_all(slist);
		g_curl.easy_cleanup(easy);
		http_error(err, errcap, "header allocation failed");
		*out_len = 0;
		out[0] = '\0';
		return SGTC_DIR_HTTP_CONNECT;
	}
	g_curl.easy_setopt(easy, SGTC_CURLOPT_HTTPHEADER, slist);

	rc = g_curl.easy_perform(easy);
	g_curl.easy_getinfo(easy, SGTC_CURLINFO_RESPONSE_CODE, &code);
	g_curl.easy_cleanup(easy);
	if (slist != NULL)
		g_curl.slist_free_all(slist);
	slist = NULL;

	if (rc == SGTC_CURLE_OK)
	{
		status = http_classify(&w, code, err, errcap);
	}
	else if (rc == SGTC_CURLE_OPERATION_TIMEDOUT)
	{
		http_error(err, errcap, "request timed out");
		status = SGTC_DIR_HTTP_TIMEOUT;
	}
	else if (rc == SGTC_CURLE_ABORTED_BY_CALLBACK && http_quit_requested())
	{
		http_error(err, errcap, "cancelled");
		status = SGTC_DIR_HTTP_CANCELLED;
	}
	else
	{
		const char *what = g_curl.easy_strerror(rc);

		http_error(err, errcap, "%s", what ? what : "transfer failed");
		status = SGTC_DIR_HTTP_CONNECT;
	}

	if (w.len < 0)
		w.len = 0;
	if (w.len > cap)
		w.len = cap;
	out[w.len] = '\0';
	*out_len = w.len;
	return status;
}

static int http_backend_load(void)
{
	static const char *names[] = {
#if defined(__APPLE__)
		"libcurl.dylib",
		"libcurl.4.dylib",
#else
		"libcurl.so.4",
		"libcurl.so",
#endif
		NULL
	};
	int i;

	for (i = 0; names[i] != NULL; i++)
	{
		void *lib = dlopen(names[i], RTLD_NOW
#if defined(RTLD_LOCAL)
			| RTLD_LOCAL
#endif
		);

		if (lib == NULL)
			continue;
		if (!http_curl_bind(lib))
		{
			dlclose(lib);
			continue;
		}
		if (g_curl.global_init != NULL
			&& g_curl.global_init(SGTC_CURL_GLOBAL_DEFAULT) != 0)
		{
			dlclose(lib);
			memset(&g_curl, 0, sizeof(g_curl));
			continue;
		}
		g_backend = "libcurl";
		return 1;
	}

	g_backend = "none";
	return 0;
}

static void http_backend_unload(void)
{
	if (g_curl.lib != NULL)
	{
		if (g_curl.global_cleanup != NULL)
			g_curl.global_cleanup();
		dlclose(g_curl.lib);
	}
	memset(&g_curl, 0, sizeof(g_curl));
}

/* --------------------------------------------------------------------- */
/* backend: WinHTTP (Windows)                                             */
/* --------------------------------------------------------------------- */

#else

static int http_backend_load(void)
{
	g_winhttp_session = WinHttpOpen(L"SGTC-BotDirector/1.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
		L"<local>", 0);
	if (g_winhttp_session == NULL)
	{
		g_backend = "none";
		return 0;
	}
	g_backend = "winhttp";
	return 1;
}

static void http_backend_unload(void)
{
	if (g_winhttp_session != NULL)
		WinHttpCloseHandle(g_winhttp_session);
	g_winhttp_session = NULL;
}

static int http_win_wide(const char *src, wchar_t *dst, int cap)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, cap);

	return n > 0;
}

// Map a WinHTTP failure to the director's status values.
static int http_win_fail(const char *what, DWORD code, char *err, int errcap)
{
	if (code == ERROR_WINHTTP_TIMEOUT)
	{
		http_error(err, errcap, "request timed out");
		return SGTC_DIR_HTTP_TIMEOUT;
	}
	http_error(err, errcap, "%s failed (%lu)", what, (unsigned long)code);
	return SGTC_DIR_HTTP_CONNECT;
}

static int http_winhttp_post(const sgtc_http_req_t *req, char *out, int cap,
	int *out_len, char *err, int errcap)
{
	wchar_t wurl[SGTC_HTTP_URL_MAX];
	wchar_t whost[256];
	wchar_t wpath[SGTC_HTTP_URL_MAX];
	wchar_t wheaders[SGTC_HTTP_KEY_MAX + 128];
	char headers[SGTC_HTTP_KEY_MAX + 128];
	URL_COMPONENTS uc;
	HINTERNET conn = NULL;
	HINTERNET request = NULL;
	sgtc_http_write_t w;
	DWORD status_code = 0;
	int status = SGTC_DIR_HTTP_CONNECT;
	size_t hlen;
	DWORD started;

	// GetTickCount (not GetTickCount64): the MinGW toolchain this DLL is built
	// with targets an older _WIN32_WINNT, so the Vista API is not declared.
	// Elapsed time is an unsigned difference, which stays correct across the
	// 49.7-day wrap of the millisecond counter.
	started = GetTickCount();

	w.buf = out;
	w.cap = cap;
	w.len = 0;
	w.overflow = 0;
	*out_len = 0;
	out[0] = '\0';

	if (!http_win_wide(req->url, wurl, SGTC_HTTP_URL_MAX))
	{
		http_error(err, errcap, "invalid URL");
		return SGTC_DIR_HTTP_CONNECT;
	}

	memset(&uc, 0, sizeof(uc));
	uc.dwStructSize = sizeof(uc);
	uc.dwSchemeLength = (DWORD)-1;
	uc.dwHostNameLength = (DWORD)-1;
	uc.dwUrlPathLength = (DWORD)-1;
	uc.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(wurl, 0, 0, &uc)
		|| (uc.nScheme != INTERNET_SCHEME_HTTP
			&& uc.nScheme != INTERNET_SCHEME_HTTPS)
		|| uc.dwHostNameLength == 0
		|| uc.dwHostNameLength >= (DWORD)(sizeof(whost) / sizeof(whost[0])))
	{
		http_error(err, errcap, "invalid URL");
		return SGTC_DIR_HTTP_CONNECT;
	}

	memcpy(whost, uc.lpszHostName,
		(size_t)uc.dwHostNameLength * sizeof(wchar_t));
	whost[uc.dwHostNameLength] = L'\0';

	if (uc.dwUrlPathLength == 0)
	{
		wpath[0] = L'/';
		wpath[1] = L'\0';
	}
	else
	{
		size_t plen = (size_t)uc.dwUrlPathLength
			+ (size_t)uc.dwExtraInfoLength;

		if (plen >= sizeof(wpath) / sizeof(wpath[0]))
		{
			http_error(err, errcap, "URL path too long");
			return SGTC_DIR_HTTP_CONNECT;
		}
		memcpy(wpath, uc.lpszUrlPath, plen * sizeof(wchar_t));
		wpath[plen] = L'\0';
	}

	headers[0] = '\0';
	http_error(headers, (int)sizeof(headers),
		"Content-Type: application/json\r\nAccept: application/json\r\n");
	if (req->key[0] != '\0')
	{
		hlen = strlen(headers);
		http_error(headers + hlen, (int)(sizeof(headers) - hlen),
			"Authorization: Bearer %s\r\n", req->key);
	}
	if (!http_win_wide(headers, wheaders,
			(int)(sizeof(wheaders) / sizeof(wheaders[0]))))
	{
		http_secure_zero(headers, sizeof(headers));
		http_secure_zero(wheaders, sizeof(wheaders));
		http_error(err, errcap, "invalid request headers");
		return SGTC_DIR_HTTP_CONNECT;
	}
	http_secure_zero(headers, sizeof(headers));

	conn = WinHttpConnect(g_winhttp_session, whost, uc.nPort, 0);
	if (conn == NULL)
	{
		return http_win_fail("WinHttpConnect", GetLastError(), err,
			errcap);
	}

	request = WinHttpOpenRequest(conn, L"POST", wpath, NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
	if (request == NULL)
	{
		status = http_win_fail("WinHttpOpenRequest", GetLastError(), err,
			errcap);
		WinHttpCloseHandle(conn);
		return status;
	}

	WinHttpSetTimeouts(request, req->timeout_ms, req->timeout_ms,
		req->timeout_ms, req->timeout_ms);

	if (!WinHttpAddRequestHeaders(request, wheaders, (DWORD)-1,
			WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
	{
		status = http_win_fail("WinHttpAddRequestHeaders", GetLastError(),
			err, errcap);
		goto done;
	}

	if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
			req->body_len > 0 ? (LPVOID)(void *)req->body : NULL,
			(DWORD)req->body_len, (DWORD)req->body_len, 0))
	{
		status = http_win_fail("WinHttpSendRequest", GetLastError(), err,
			errcap);
		goto done;
	}

	if (!WinHttpReceiveResponse(request, NULL))
	{
		status = http_win_fail("WinHttpReceiveResponse", GetLastError(),
			err, errcap);
		goto done;
	}

	{
		DWORD qlen = sizeof(status_code);

		if (!WinHttpQueryHeaders(request,
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &qlen,
				WINHTTP_NO_HEADER_INDEX))
		{
			status = http_win_fail("WinHttpQueryHeaders",
				GetLastError(), err, errcap);
			goto done;
		}
	}

	for (;;)
	{
		DWORD avail = 0;
		DWORD want;
		DWORD got = 0;
		char chunk[8192];

		if (http_quit_requested())
		{
			http_error(err, errcap, "cancelled");
			status = SGTC_DIR_HTTP_CANCELLED;
			goto done;
		}
		if ((DWORD)(GetTickCount() - started) > (DWORD)req->timeout_ms)
		{
			http_error(err, errcap, "request timed out");
			status = SGTC_DIR_HTTP_TIMEOUT;
			goto done;
		}
		if (!WinHttpQueryDataAvailable(request, &avail))
		{
			status = http_win_fail("WinHttpQueryDataAvailable",
				GetLastError(), err, errcap);
			goto done;
		}
		if (avail == 0)
			break;

		want = avail;
		if (want > (DWORD)sizeof(chunk))
			want = (DWORD)sizeof(chunk);
		if (!WinHttpReadData(request, chunk, want, &got))
		{
			status = http_win_fail("WinHttpReadData", GetLastError(),
				err, errcap);
			goto done;
		}
		if (got == 0)
			break;
		http_body_append(&w, chunk, (size_t)got);
	}

	if (http_quit_requested())
	{
		http_error(err, errcap, "cancelled");
		status = SGTC_DIR_HTTP_CANCELLED;
		goto done;
	}

	status = http_classify(&w, (long)status_code, err, errcap);

done:
	http_secure_zero(wheaders, sizeof(wheaders));
	if (request != NULL)
		WinHttpCloseHandle(request);
	if (conn != NULL)
		WinHttpCloseHandle(conn);
	if (w.len < 0)
		w.len = 0;
	if (w.len > cap)
		w.len = cap;
	out[w.len] = '\0';
	*out_len = w.len;
	return status;
}

#endif

static int http_backend_post(const sgtc_http_req_t *req, char *out, int cap,
	int *out_len, char *err, int errcap)
{
#if defined(_WIN32)
	return http_winhttp_post(req, out, cap, out_len, err, errcap);
#else
	return http_curl_post(req, out, cap, out_len, err, errcap);
#endif
}

/* --------------------------------------------------------------------- */
/* worker + thread plumbing                                               */
/* --------------------------------------------------------------------- */

// Runs with the request owned by this thread (g_have_req cleared, g_busy set).
static void http_worker_one(void)
{
	sgtc_dir_http_done_fn done;
	void *ctx;
	char err[SGTC_HTTP_ERR_MAX];
	int status;
	int len = 0;

	err[0] = '\0';
	g_slot_body[0] = '\0';

	if (http_quit_requested())
	{
		status = SGTC_DIR_HTTP_CANCELLED;
		http_error(err, sizeof(err), "cancelled");
	}
	else
	{
		status = http_backend_post(&g_req, g_slot_body, SGTC_HTTP_RESP_MAX,
			&len, err, sizeof(err));
		if (http_quit_requested())
		{
			status = SGTC_DIR_HTTP_CANCELLED;
			len = 0;
			g_slot_body[0] = '\0';
			http_error(err, sizeof(err), "cancelled");
		}
	}

	if (len < 0)
		len = 0;
	if (len > SGTC_HTTP_RESP_MAX)
		len = SGTC_HTTP_RESP_MAX;
	g_slot_body[len] = '\0';

	// The key dies here, before the result becomes visible to the game thread.
	http_secure_zero(g_req.key, sizeof(g_req.key));
	http_secure_zero(g_req.body, sizeof(g_req.body));
	done = g_req.done;
	ctx = g_req.ctx;
	g_req.body_len = 0;
	g_req.done = NULL;
	g_req.ctx = NULL;

	http_lock();
	g_slot_len = len;
	g_slot_status = status;
	g_slot_error[0] = '\0';
	if (err[0] != '\0')
		http_copy(g_slot_error, (int)sizeof(g_slot_error), err);
	g_slot_done = done;
	g_slot_ctx = ctx;
	g_slot_ready = 1;
	http_unlock();
}

static void http_worker_run(void)
{
	for (;;)
	{
		http_lock();
		while (!g_have_req && !http_quit_requested())
			http_wait();

		if (http_quit_requested())
		{
			// A queued request still owns exactly one callback.
			if (g_have_req)
			{
				g_have_req = 0;
				http_secure_zero(g_req.key, sizeof(g_req.key));
				http_secure_zero(g_req.body, sizeof(g_req.body));
				g_slot_body[0] = '\0';
				g_slot_len = 0;
				g_slot_status = SGTC_DIR_HTTP_CANCELLED;
				http_copy(g_slot_error, (int)sizeof(g_slot_error),
					"cancelled");
				g_slot_done = g_req.done;
				g_slot_ctx = g_req.ctx;
				g_req.done = NULL;
				g_req.ctx = NULL;
				g_req.body_len = 0;
				g_slot_ready = 1;
			}
			http_unlock();
			return;
		}

		g_have_req = 0;
		http_unlock();

		http_worker_one();
	}
}

#if defined(_WIN32)

static DWORD WINAPI http_worker_thread(LPVOID)
{
	http_worker_run();
	return 0;
}

static int http_thread_start(void)
{
	DWORD id = 0;

	g_thread = CreateThread(NULL, 0, http_worker_thread, NULL, 0, &id);
	return g_thread != NULL;
}

static void http_thread_join(void)
{
	if (g_thread != NULL)
	{
		WaitForSingleObject(g_thread, INFINITE);
		CloseHandle(g_thread);
		g_thread = NULL;
	}
}

static int http_sync_init(void)
{
	InitializeCriticalSection(&g_lock);
	g_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_wake == NULL)
	{
		DeleteCriticalSection(&g_lock);
		return 0;
	}
	return 1;
}

static void http_sync_destroy(void)
{
	if (g_wake != NULL)
		CloseHandle(g_wake);
	g_wake = NULL;
	DeleteCriticalSection(&g_lock);
}

#else

static void *http_worker_thread(void *)
{
	http_worker_run();
	return NULL;
}

static int http_thread_start(void)
{
	return pthread_create(&g_thread, NULL, http_worker_thread, NULL) == 0;
}

static void http_thread_join(void)
{
	pthread_join(g_thread, NULL);
}

static int http_sync_init(void)
{
	if (pthread_mutex_init(&g_lock, NULL) != 0)
		return 0;
	if (pthread_cond_init(&g_wake, NULL) != 0)
	{
		pthread_mutex_destroy(&g_lock);
		return 0;
	}
	return 1;
}

static void http_sync_destroy(void)
{
	pthread_cond_destroy(&g_wake);
	pthread_mutex_destroy(&g_lock);
}

#endif

/* --------------------------------------------------------------------- */
/* public API                                                             */
/* --------------------------------------------------------------------- */

int SGTC_Dir_HttpInit(void)
{
	if (g_inited)
		return g_available;

	memset(&g_req, 0, sizeof(g_req));
	g_busy = 0;
	g_have_req = 0;
	g_slot_ready = 0;
	g_slot_len = 0;
	g_slot_body[0] = '\0';
	g_slot_error[0] = '\0';
	g_slot_done = NULL;
	g_slot_ctx = NULL;
	g_cb_body[0] = '\0';
	g_available = 0;
	g_backend = "none";
	g_inited = 1;
	http_set_quit(0);

	if (!http_sync_init())
	{
		g_inited = 0;
		return 0;
	}
	g_sync_ready = 1;

	if (!http_backend_load())
		return 0;

	if (!http_thread_start())
	{
		http_backend_unload();
		g_backend = "none";
		return 0;
	}
	g_thread_started = 1;
	g_available = 1;
	return 1;
}

void SGTC_Dir_HttpShutdown(void)
{
	sgtc_dir_http_done_fn done = NULL;
	void *ctx = NULL;
	char err[SGTC_HTTP_ERR_MAX];

	err[0] = '\0';

	if (!g_inited)
		return;

	if (g_sync_ready)
	{
		http_lock();
		http_set_quit(1);
		http_wake();
		http_unlock();
	}

	if (g_thread_started)
	{
		http_thread_join();
		g_thread_started = 0;
	}

	if (g_sync_ready)
	{
		http_lock();
		if (g_slot_ready)
		{
			done = g_slot_done;
			ctx = g_slot_ctx;
			http_copy(err, (int)sizeof(err), "shutdown");
			g_slot_ready = 0;
			g_slot_done = NULL;
			g_slot_ctx = NULL;
			g_slot_len = 0;
			g_slot_body[0] = '\0';
		}
		g_busy = 0;
		g_have_req = 0;
		http_unlock();
	}

	g_available = 0;
	g_backend = "none";

	// Exactly-once: whatever was still pending is reported as cancelled.
	if (done != NULL)
		done(ctx, SGTC_DIR_HTTP_CANCELLED, "", 0, err);

	http_backend_unload();

	if (g_sync_ready)
	{
		http_sync_destroy();
		g_sync_ready = 0;
	}

	http_secure_zero(g_req.key, sizeof(g_req.key));
	http_secure_zero(g_req.body, sizeof(g_req.body));
	memset(&g_req, 0, sizeof(g_req));
	g_slot_len = 0;
	g_slot_body[0] = '\0';
	g_slot_error[0] = '\0';
	http_set_quit(0);
	g_inited = 0;
}

int SGTC_Dir_HttpAvailable(void)
{
	return g_available;
}

const char *SGTC_Dir_HttpBackend(void)
{
	return g_backend;
}

int SGTC_Dir_HttpBusy(void)
{
	int busy;

	if (!g_inited || !g_sync_ready)
		return 0;
	http_lock();
	busy = g_busy;
	http_unlock();
	return busy;
}

int SGTC_Dir_HttpPost(const char *url, const char *key, const char *json_body,
	int body_len, int timeout_ms, sgtc_dir_http_done_fn done, void *ctx)
{
	int key_len = 0;
	int url_len;

	if (!g_inited || !g_available || !g_sync_ready)
		return -1;
	if (done == NULL || !http_url_ok(url))
		return -1;
	if (body_len < 0 || body_len > SGTC_HTTP_REQ_MAX)
		return -1;
	if (json_body == NULL && body_len > 0)
		return -1;
	if (key != NULL)
	{
		key_len = http_bounded_len(key, SGTC_HTTP_KEY_MAX);
		if (key_len < 0 || !http_key_ok(key))
			return -1;
	}
	url_len = http_bounded_len(url, SGTC_HTTP_URL_MAX);

	http_lock();
	if (g_busy || !g_available || http_quit_requested())
	{
		http_unlock();
		return -1;
	}

	memcpy(g_req.url, url, (size_t)url_len + 1);
	if (key != NULL)
		memcpy(g_req.key, key, (size_t)key_len + 1);
	else
		g_req.key[0] = '\0';
	if (body_len > 0)
		memcpy(g_req.body, json_body, (size_t)body_len);
	g_req.body_len = body_len;
	g_req.timeout_ms = http_timeout_value(timeout_ms);
	g_req.done = done;
	g_req.ctx = ctx;
	g_have_req = 1;
	g_busy = 1;
	http_wake();
	http_unlock();
	return 0;
}

int SGTC_Dir_HttpPoll(void)
{
	sgtc_dir_http_done_fn done;
	void *ctx;
	int status;
	int len;
	int has_error;
	char err[SGTC_HTTP_ERR_MAX];

	if (!g_inited || !g_sync_ready)
		return 0;

	http_lock();
	if (!g_slot_ready)
	{
		http_unlock();
		return 0;
	}

	done = g_slot_done;
	ctx = g_slot_ctx;
	status = g_slot_status;
	len = g_slot_len;
	has_error = g_slot_error[0] != '\0';
	if (has_error)
		http_copy(err, (int)sizeof(err), g_slot_error);
	if (len > 0)
		memcpy(g_cb_body, g_slot_body, (size_t)len);
	g_cb_body[len] = '\0';
	g_slot_ready = 0;
	g_slot_done = NULL;
	g_slot_ctx = NULL;
	// The request is finished: clear busy before the callback so it may Post
	// the next one. The body handed to the callback lives in g_cb_body, which
	// the worker never touches.
	g_busy = 0;
	http_unlock();

	if (done != NULL)
		done(ctx, status, g_cb_body, len, has_error ? err : NULL);

	g_cb_body[0] = '\0';
	return 1;
}
