// sgtc_director_http.h - non-blocking HTTP POST for the bot director.
//
// The game frame must never wait on the network. SGTC_Dir_HttpPost() copies
// everything it needs, starts (or queues onto) a single worker thread and
// returns immediately; SGTC_Dir_HttpPoll(), called from StartFrame, invokes the
// completion callback on the game thread for at most one finished request per
// call. Only one request is ever in flight: a second Post while busy fails.
//
// Backends, selected at runtime:
//   * Linux:   libcurl loaded with dlopen("libcurl.so.4") -- there is no
//              link-time dependency, and a missing library is reported once by
//              the caller through SGTC_Dir_HttpAvailable().
//   * Windows: WinHTTP (winhttp.dll), linked normally.
//   * Apple:   libcurl via dlopen("libcurl.dylib"), used by the host harness.
//
// The request body and the API key are held only in the worker's private
// memory and freed before the callback runs; neither is ever logged here.
#pragma once
#ifndef SGTC_DIRECTOR_HTTP_H
#define SGTC_DIRECTOR_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

// status values passed to the completion callback.
enum
{
	SGTC_DIR_HTTP_OK = 0,          // 2xx and a body was received
	SGTC_DIR_HTTP_BAD_STATUS = 1,  // non-2xx response, body holds the error text
	SGTC_DIR_HTTP_TIMEOUT = 2,
	SGTC_DIR_HTTP_CONNECT = 3,     // DNS/TCP/TLS failure, see error
	SGTC_DIR_HTTP_TOO_BIG = 4,     // reply exceeded the body cap (truncated)
	SGTC_DIR_HTTP_CANCELLED = 5    // shutdown while in flight
};

// Called on the game thread from SGTC_Dir_HttpPoll(). `body` is NUL-terminated
// and valid only for the duration of the callback; `error` is NULL on success.
typedef void (*sgtc_dir_http_done_fn)( void *ctx, int status, const char *body,
	int body_len, const char *error );

// Load the backend. Safe to call twice. Returns 1 when a backend is usable,
// 0 when not (the director then runs local-only and logs once).
int SGTC_Dir_HttpInit( void );

// Join the worker (cancelling any in-flight request) and release the backend.
// Safe to call when Init was never called.
void SGTC_Dir_HttpShutdown( void );

int SGTC_Dir_HttpAvailable( void );

// "libcurl" / "winhttp" / "none" -- for the one-shot startup log and status.
const char *SGTC_Dir_HttpBackend( void );

int SGTC_Dir_HttpBusy( void );

// Start POST <url> with a JSON body. `key` may be NULL (a keyless local
// endpoint); when set it is sent as "Authorization: Bearer <key>". `timeout_ms`
// bounds the whole request (0 -> 15000). The callback is invoked exactly once
// from SGTC_Dir_HttpPoll(), including for every failure mode above.
// Returns 0 when the request was accepted, -1 when busy or unavailable.
int SGTC_Dir_HttpPost( const char *url, const char *key, const char *json_body,
	int body_len, int timeout_ms, sgtc_dir_http_done_fn done, void *ctx );

// Drain at most one completed request (invokes its callback) and reap the
// worker thread. Call once per frame. Returns 1 when a callback ran.
int SGTC_Dir_HttpPoll( void );

#ifdef __cplusplus
}
#endif

#endif // SGTC_DIRECTOR_HTTP_H
