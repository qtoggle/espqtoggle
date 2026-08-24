# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

espQToggle is firmware for ESP8266/ESP8285 devices implementing the
[qToggle API](https://github.com/qtoggle/docs/wiki/API-Specifications). It is bare-metal C99 against the
Espressif NON-OS SDK — no RTOS, no libc beyond the SDK's, roughly 40 KB of usable heap and a 4 KB stack.

## Building

Cross-compilation only; there is no host build and no way to run this code natively.

Prerequisites the Makefile hard-checks for:
- `xtensa-lx106-elf-gcc` on `PATH`
- `SDK_BASE` pointing at an ESP8266 NON-OS SDK checkout
- that SDK patched via `builder/fix-sdk.sh $SDK_BASE` — the build aborts if `$SDK_BASE/.fixed` is missing

```bash
./build.sh                  # convenience wrapper: sets toolchain/SDK paths under /opt/esp8266, DEBUG=true, make clean && make
make                        # -> build/firmware.bin (full 1 MB image) + build/user1.bin, build/user2.bin (OTA slots)
make clean
make V=1                    # echo full compiler command lines
VERSION=1.2.3-beta.1 make   # version string is parsed into FW_VERSION_* defines; defaults to 0.0.0-unknown.0
DEBUG=false make            # release build — note this *adds* -Werror, so release builds must be warning-clean
```

Docker (this is what CI uses):

```bash
docker build -t qtoggle/espqtoggle-builder builder/
docker run --rm -v $PWD:/src qtoggle/espqtoggle-builder
```

Build-time feature toggles (make variables → `-D_OTA`, `-D_SSL`, `-D_SLEEP`, `-D_BATTERY`): `OTA`, `SSL`,
`SLEEP`, `BATTERY`. Code guarded by these must compile with the feature both on and off.

`DEBUG_FLAGS` is a list of subsystem names turned into `-D_DEBUG_<FLAG>` defines; each subsystem's
`DEBUG_<NAME>(...)` macro compiles to nothing unless its flag is listed. Setting `DEBUG_IP`/`DEBUG_PORT`
redirects debug output from UART to UDP.

## Tests

There are **no unit tests and no host-side test harness** — an ESP8266 cannot be emulated. The only tests are
blackbox tests run on real hardware through [TestMyESP](https://gitlab.com/ccrisan/testmyesp/-/wikis/home), so
they need both a physical device and service credentials. There is no way to run them locally without those.

### How TestMyESP works

A real ESP8266 board is wired to a Raspberry Pi that controls its power, reset line, GPIOs and ADC, talks to it
over serial (also used to flash it), and acts as either a WiFi AP or a station to reach it over the network.
The Pi runs a web service; you `POST /jobs` and a background worker executes the job against the board.

The model is three levels deep:

- **Job** — an ordered list of test cases plus the flash images and assets they share. Has a `timeout` and
  `continue_on_failure`. States: `pending` → `flashing` → `executing` → `succeeded`/`failed`/`cancelled`.
- **Test case** — an ordered list of instructions with a unique `name`. Aborts at the first failing
  instruction; the rest are skipped.
- **Instruction** — one action with a `name` and `params`. Each either succeeds or fails.

**State is deliberately not reset between test cases in a job.** This keeps jobs fast and limits flash wear
(~100k write cycles), but it means test cases are order-dependent: a case inherits whatever device state the
previous one left behind. The numeric directory/file prefixes under `test/test-cases/` are what pin that order.
A case can opt out with `reset_before` (reset the device) or `restart_before` (power-cycle it).

### Running them

`test/testmyesp-helper.sh` builds the job JSON and submits it; it needs `curl`, `openssl`, `jq` and `envsubst`,
and mints the service JWT from `--credentials` itself.

```bash
# a single test case — just point --test-cases at one file
./test/testmyesp-helper.sh --server-url http://testmyesp.qtoggle.io:9999 --credentials USER:PASS \
    --flash-image-file user1.bin 0x01000 build/user1.bin \
    --flash-image-file user2.bin 0x81000 build/user2.bin \
    --flash-image-fill config.bin 0x7C000 0 16384 \
    --test-cases test/test-cases/02-api/09-port-value/01-get-port-value.json

# a whole suite, with failure detail
./test/testmyesp-helper.sh ... --test-cases test/test-cases/02-api/*/*.json \
    --verbose --show-serial-log --continue-on-failure
```

`.github/workflows/main.yml` holds the canonical full invocation — the complete flash-image set (bootloader,
SDK init data, sysparam, both OTA slots, zeroed config) and every environment variable the suite expects. Copy
from there rather than reconstructing it. Flash addresses must match the `FLASH_*_ADDR` values in the Makefile.

`test/run-tests.sh` is a personal scratch script with hardcoded credentials and most invocations commented
out. Read it for flag shapes; don't treat it as the entry point.

### Writing test cases

A test case file is just the test-case object — the helper wraps it into a job:

```json
{
    "name": "get-firmware-security",
    "instructions": [
        {
            "name": "json-http-client",
            "params": {
                "method": "GET",
                "path": "/firmware",
                "headers": {"Authorization": "Bearer ${TEST_JWT_VIEWONLY}"},
                "expected_status": 403
            }
        }
    ]
}
```

Optional test-case keys: `reset_before`, `restart_before`, `serial_baud`, `serial_parity`, `serial_stop_bits`,
`ensure_flash_images`. Optional per-instruction keys: `fire_and_forget` (don't wait for it) and `fire_delay`.

The instructions this suite actually uses, out of the [full set](https://gitlab.com/ccrisan/testmyesp/-/wikis/Instructions):

| Instruction | Use here |
| :--- | :--- |
| `json-http-client` | The workhorse — ~580 of ~700 instructions. Validates `expected_status`, `expected_body` or `expected_body_schema`. |
| `json-http-server` / `http-server` | Stands up a server on the Pi to catch outbound requests — this is how webhooks are tested. |
| `device-reset`, `sleep` | Reboot and settle between steps. |
| `device-write-gpio`, `device-check-gpio`, `device-float-gpio`, `device-write-adc` | Drive and observe pins for the GPIO/ADC peripheral suites. |
| `wifi-ap-start`/`-stop`, `wifi-ap-wait-device-{connect,disconnect}`, `wifi-station-connect` | Pi as AP (device connects out) or as station (device runs its own AP, e.g. setup mode). |
| `reset-serial-log`, `check-serial` | Assert on debug/UART output; `check-serial` supports text, hex, base64 or regex matching. |

### Variables

Two substitution mechanisms, and mixing them up is a common mistake:

- **`${VAR}` — client-side.** `envsubst` expands these from the calling shell *before* the job is submitted, so
  every one must be exported. The suite expects `TEST_JWT_ADMIN`, `TEST_JWT_ADMIN_EMPTY`, `TEST_JWT_NORMAL`,
  `TEST_JWT_VIEWONLY`, `TEST_JWT_WEBHOOKS`, `TEST_SSID`, `TEST_PSK`, `TEST_PASSWORD`, `TEST_NETWORK_*`,
  `VERSION` and `FW_CONFIG_NAME`. The `TEST_JWT_*` tokens are pre-signed qToggle tokens whose secret is the
  SHA-256 of `TEST_PASSWORD` (or of the empty string, for `_ADMIN_EMPTY`) — changing `TEST_PASSWORD` without
  regenerating them breaks every authenticated case. The helper also injects `SERVER_URL` and
  `SERVER_URL_HTTP`.
- **`{{VAR}}` — server-side.** Substituted by the TestMyESP server at run time, for values unknown when the
  test was written: `DEVICE_SERIAL_NUMBER`, `DEVICE_IP_ADDRESS`, `HOST_IP_ADDRESS`, `HOST_MAC_ADDRESS_UPPER`,
  `TEST_CASE_NAME`, `JOB_ID`, and more. Always keep these inside JSON double quotes — the server converts to
  numbers where needed.
- **`${DOLLAR}` yields a literal `$`.** Used heavily here, because qToggle port expressions reference ports as
  `$port_id` and an unescaped `$` would be eaten by `envsubst`.

### CI

`.github/workflows/main.yml` runs the blackbox job **only** on `workflow_dispatch` or a `version-*` tag.
Ordinary pushes build nothing, so a green push is not evidence that the code even compiles.

## Architecture

Two layers, deliberately separated:

- **`src/espgoodies/`** — a reusable ESP8266 support library with no qToggle knowledge: JSON, HTTP server and
  client, TCP/DNS servers, crypto (SHA-1/SHA-256/HMAC/base64), JWT, WiFi, OTA, flash config, sleep, RTC, plus
  `drivers/` (gpio, uart, hspi, pwm, onewire).
- **`src/`** — the firmware proper.

### Execution model

`user_init()` (user_main.c) brings up system/WiFi/config, then `on_system_ready()` starts the HTTP server
(`client_init`) and enables polling once WiFi connects.

There is no scheduler. `core.c` drives everything off the SDK task queue: `core_poll()` reschedules itself via
`system_task_schedule(TASK_ID_POLL)`, producing a continuous cooperative loop. **Nothing may block** — deferred
work goes through `os_timer` or `call_later()`.

### Data model

- **Peripheral** (`peripherals.c`, `src/peripherals/*.c`) — a piece of hardware. The registry is
  `all_peripheral_types[]` in `peripherals.c`, indexed by `type_id` 1..`PERIPHERAL_MAX_TYPE_ID`. Each type
  implements `peripheral_type_t` = `{init, cleanup, make_ports, handle_setup_mode}`. Adding a type means: new
  file under `src/peripherals/`, append to that array, bump `PERIPHERAL_MAX_TYPE_ID`. Per-peripheral settings
  live in a fixed 56-byte `params` blob accessed through the `PERIPHERAL_PARAM_*` macros.
- **Port** (`ports.c`, `virtual.c`) — one readable/writable value exposed over the API. Peripherals create
  their ports in `make_ports`. Each port owns a `slot`, which is its bit index in the 64-bit change masks.
- **Expression** (`expr.c`) — a small functional language attached to a port as `expression`,
  `transform_read` or `transform_write`. Parsed into an `expr_t` tree; the function table is `funcs[]`.
  `$port_id` reads another port; `expr_check_loops()` rejects cycles. Stateful functions (`DELAY`, `FMAVG`,
  `FMEDIAN`) keep state in `expr->paux`/`expr->aux` and are cleaned up through `func_needs_free()`, which
  calls the function's own callback with `argc == -1`.

### Change propagation

`core_poll()` samples every enabled port that is due, accumulates a 64-bit `change_mask` of changed slots, and
hands it to `handle_value_changes()`, which re-evaluates expressions whose dependency mask
(`expr_get_port_deps`) intersects it, then emits events. Bits 63 and 62 (`TIME_EXPR_DEP_BIT`,
`TIME_MS_EXPR_DEP_BIT` in `core.h`) are pseudo-slots that force re-evaluation of time-dependent expressions
each tick.

Events (`events.c`) fan out to two independent consumers: long-polling API sessions (`sessions.c`) and
outbound webhooks (`webhooks.c`). `EVENT_ACCESS_LEVELS[]` gates which access level may observe each type.

### HTTP / API

`client.c` is the front end: accepts the connection, feeds bytes into the `httpserver.c` character-at-a-time
state machine, authenticates, then calls `api_call_handle()`. Note that `/listen` is handled entirely inside
`client.c` and never reaches `api.c`.

`api.c` (~4000 lines) splits the path into at most three segments and dispatches to `api_{get,post,patch,put,
delete}_*`.

Auth is a bearer JWT (HS256). The signing secret is the **SHA-256 hex digest of the corresponding password**,
one per access level (`admin` / `normal` / `viewonly`). Two paths bypass this deliberately: setup mode grants
admin unconditionally, and an empty admin password grants admin to unauthenticated requests.

### Persistence

`flashcfg.c` provides two fixed flash regions: `FLASH_CONFIG_SLOT_DEFAULT` (8 KB, device configuration) and
`FLASH_CONFIG_SLOT_SYSTEM` (4 KB). The device configuration is one flat binary blob whose byte offsets are
hardcoded as `CONFIG_OFFS_*` in `config.h`: 96 bytes per port × 32 slots, 64 bytes per peripheral × 16, and a
3584-byte string pool at the end holding all variable-length strings, referenced by 4-byte offsets
(`stringpool.c`).

**Changing any `CONFIG_OFFS_*` or size constant invalidates every deployed device's saved configuration.** The
only migration mechanism is `check_update_fw_config()` in `user_main.c`, which resets config when the stored
firmware version doesn't match.

Writes are deferred: `config_mark_for_saving()` sets a flag that `core_poll()` flushes at most once every
5 seconds; `config_ensure_saved()` forces it (used before reset/sleep).

Devices with a `config_name` set fetch `FW_BASE_URL/FW_BASE_CFG_PATH/<config_name>.json` at boot and apply it
(`config_start_auto_provisioning`).

## Conventions

- **`espgoodies/common.h` redefines the allocators.** `malloc`/`realloc`/`zalloc`/`free`/`strdup`/`strndup`/
  `strtok` map to `my_*` wrappers. `my_malloc` **reboots the device** on failure, which is why allocation
  results are deliberately never null-checked. Don't "fix" that by adding null checks; do keep allocations
  small and bounded.
- **`ICACHE_FLASH_ATTR` goes on the declaration, not the definition** — in the header for public functions, on
  the top-of-file forward declaration for statics. It places code in flash rather than IRAM. Omitting it wastes
  scarce IRAM; applying it to anything reachable from an interrupt will crash the device.
- **JSON ownership is encoded in `free_mode`.** `json_dump()` returns a fresh buffer; `json_dump_r()` returns a
  shared static one that the next call invalidates. `JSON_FREE_EVERYTHING` consumes the tree — most response
  paths use it, and must not touch the tree afterwards.
- Style: 4-space indent, 120-column limit, `snake_case`, opening brace on the same line, `else` on its own
  line. Match the surrounding file.
- `html/index.html` is gzipped and embedded into the firmware image by `gen_appbin.py`; it is served on 404s
  while in setup mode.
- `.cproject`/`.settings/` are Eclipse CDT files, not part of the build.

## Known issues

`CODE_REVIEW.md` tracks an audited backlog of memory-safety and correctness bugs, each with a stable ID
(`C1`…, `H1`…, `M1`…), exact file:line, and a suggested fix. Check it before touching the JSON parser, the
expression parser, `httpclient.c`, or the auth path — several known-broken spots are documented there rather
than fixed. Reference the IDs in commit messages and tick items off as they are addressed.
