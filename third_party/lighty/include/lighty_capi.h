/*
 * lighty_capi.h — C ABI for LightyLauncherLib (static library).
 *
 * Copyright (c) 2025 XGstudio
 * SPDX-License-Identifier: MIT
 *
 * Build:  cargo build --release -p lighty-capi
 * Link:   lighty_capi.lib + bcrypt ws2_32 crypt32 secur32 userenv advapi32
 *         ole32 oleaut32 ntdll kernel32 (dynamic CRT, /MD)
 *
 * Strings are UTF-8. Out-parameters are caller-owned buffers; a negative
 * return code means "nothing written".
 */

#ifndef LIGHTY_CAPI_H
#define LIGHTY_CAPI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* return codes */
#define LIGHTY_OK              0
#define LIGHTY_ERR            -1
#define LIGHTY_BUFFER_TOO_SMALL (-2)

/* lighty_job_poll results */
#define LIGHTY_POLL_EVENT      1
#define LIGHTY_POLL_IDLE       0
#define LIGHTY_POLL_FINISHED   2
#define LIGHTY_POLL_FAILED     3

typedef struct LightyLaunchJob LightyLaunchJob;

/* Library version, e.g. "26.11.0". Static storage, do not free. */
const char *lighty_version(void);

/*
 * Initialise global launcher paths. Call once, before anything else.
 * `name` is joined under the OS data dir (%APPDATA%\<name> on Windows),
 * so pass "XGStudio\\VoidCraftLauncher".
 * Returns 0 ok, 1 already initialised, -1 error.
 */
int32_t lighty_init(const char *name);

/*
 * Derives the offline UUID ("OfflinePlayer:<name>", v5) into `uuid_buf`.
 * Returns 0 / LIGHTY_BUFFER_TOO_SMALL.
 */
int32_t lighty_offline_uuid(const char *username,
                            char *uuid_buf, size_t uuid_cap);

/*
 * Starts a launch job (installs what is missing, then starts the JVM).
 * Returns NULL on a malformed request.
 *
 * request_json:
 * {
 *   "instance": "1.21.1-vanilla",
 *   "loader": "vanilla",        // vanilla|fabric|quilt|forge|neoforge|optifine
 *   "loader_version": "",
 *   "mc_version": "1.21.1",
 *   "player": "Steve",
 *   "memory_mb": 4096,
 *   "width": 1280,
 *   "height": 720,
 *   "game_dir": "D:\\Games\\MC",  // optional
 *   "java": "temurin"           // temurin|graalvm|zulu|liberica
 * }
 */
LightyLaunchJob *lighty_job_launch(const char *request_json);

/*
 * Drains one event as JSON (serde representation of lighty_event::Event,
 * e.g. {"type":"Launch","data":{"InstallProgress":{...}}}).
 * Returns LIGHTY_POLL_EVENT / LIGHTY_POLL_IDLE / LIGHTY_POLL_FINISHED /
 * LIGHTY_POLL_FAILED / LIGHTY_BUFFER_TOO_SMALL / LIGHTY_ERR.
 * Call in a loop until it returns IDLE, FINISHED or FAILED.
 */
int32_t lighty_job_poll(LightyLaunchJob *job, char *buf, size_t cap);

/* Error text of a finished job (empty string when it succeeded). */
int32_t lighty_job_error(LightyLaunchJob *job, char *buf, size_t cap);

/* Aborts a running job; the handle stays valid until lighty_job_free. */
int32_t lighty_job_cancel(LightyLaunchJob *job);

/* Releases the handle. */
void lighty_job_free(LightyLaunchJob *job);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTY_CAPI_H */
