# Mouse Debounce for macOS

A small, source-auditable macOS utility for repairing worn mouse-button chatter in software.

Version **0.6.0** provides:

- Linux `franzos/mouse-debounce`-style short-release repair;
- minimal CoreGraphics event-tap plumbing inspired by Vorssaint;
- left/right/middle filtering by default;
- per-button timing inheritance;
- live finite measurement with statistics/recommendations;
- conservative missing-wheel-pulse diagnostics;
- a human-readable persistent config;
- launchd lifecycle control.

## Quick start

On macOS with Apple command-line developer tools:

```sh
make app
tools/mousedebouncectl install
tools/mousedebouncectl grant
tools/mousedebouncectl start
```

When the Accessibility pane opens, enable **Mouse Debounce** before starting the service. Installation registers the launch agent but does not start it or make it start automatically at login (`RunAtLoad` is false).

Development builds use ad-hoc signing by default. To reduce repeated Accessibility approval prompts, see [Stable local code signing](docs/local-signing.md).

## Debounce algorithm

MouseDebounce uses two separate time windows:

- `short-ms` decides whether a press is suspiciously short. It measures from the
  physical **Down** to the following physical **Up**.
- `hold-ms` is the extra observation period after a suspicious Up. It gives the
  button time to bounce back Down before the Up is delivered to the application.

A Down is delivered immediately. If its Up arrives in less than `short-ms`, that
Up is temporarily withheld. A returning Down before the `hold-ms` deadline makes
the withheld Up and returning Down a bounce pair, so both are discarded and the
application continues to see one uninterrupted press. If no Down returns, the Up
is delivered when the deadline expires. An Up after a press lasting at least
`short-ms` is delivered immediately, without the extra hold delay. Duplicate
Downs are also suppressed while the application already considers the button
down.

For example, with `--short-ms 20 --hold-ms 20`:

```text
0 ms   Down  -> delivered immediately
8 ms   Up    -> press was shorter than 20 ms; hold this Up until 28 ms
15 ms  Down  -> before 28 ms, so discard this Down and the held Up as chatter
```

Without the returning Down at 15 ms, the held Up would be delivered at 28 ms. By
contrast, a separate press with Down at 0 ms and Up at 35 ms is delivered without
any extra delay. After a bounce pair, the next press duration is measured from
the returning Down (15 ms in this example).

Defaults:

```text
short-ms = 20
hold-ms  = 20
buttons  = left,right,middle
```

Tuning is a tradeoff. Raising `short-ms` makes more brief presses eligible for
filtering. Raising `hold-ms` catches bounce that returns later, but also delays
the release of eligible presses for longer. Real, very fast clicks or intentional
rapid re-clicks can resemble switch chatter: a lone fast click is preserved but
its Up is delayed, while a fast Up/Down pair inside the hold window can be merged
into one continuous press. Start near the defaults and use measurement evidence
from the faulty button before widening either window.

## Timing clock

MouseDebounce does **not** use `CGEventGetTimestamp()` for debounce or measurement intervals.

Each callback is timestamped with macOS `CLOCK_UPTIME_RAW`, which directly returns monotonic nanoseconds. This avoids assumptions about Quartz event-timestamp representation or Mach timebase conversion. A `20 ms` debounce setting therefore means approximately 20 ms of real elapsed callback-receipt time.

When a withheld Up is reposted, its Quartz timestamp is refreshed with a native CoreGraphics timestamp rather than converting the monotonic clock into Quartz units.

## Per-button timing and inheritance

Buttons can have separate values:

```text
--left-short-ms 18 --left-hold-ms 16
--right-short-ms 20 --right-hold-ms 22
--middle-short-ms 25 --middle-hold-ms 20
```

Global options set all buttons:

```text
--short-ms 20 --hold-ms 20
```

Later arguments win. If a per-button value is unset and no global value supplied, it inherits the arithmetic mean of explicitly configured sibling buttons. With no configured siblings it uses the 20 ms default.

## Persistent config

Default path:

```text
~/Library/Application Support/MouseDebounce/config.args
```

The format is deliberately just app arguments plus optional `#` comments:

```text
--buttons left,right,middle
--short-ms 20
--hold-ms 20
--sound-volume 0.1
```

Config loads first and CLI arguments override it.

`--sound-volume` accepts `0` through `1` and is saved like the other settings;
`0` silences all sounds. `--debug` enables startup, scroll-down, and filter
diagnostic sounds; without it, normal filtering is silent. `--debug` is a
command-line switch, not a saved setting.

Recommended save command:

```sh
tools/mousedebouncectl save --short-ms 20 --hold-ms 20
```

This calls `--save-config-and-exit`, so no extra long-running process remains. If the launchd service was running, the controller restarts it so the new config takes effect.

View saved settings:

```sh
tools/mousedebouncectl config
```

## Measurement

Recommended interface:

```sh
tools/mousedebouncectl measure 60
```

This command:

1. **stops the debounce service first**, so measurement sees raw mouse events rather than filtered/reposted events;
2. launches the installed `.app` through LaunchServices;
3. mirrors the measurement log to the terminal live;
4. auto-exits after the requested duration;
5. restores the debounce service only if it had been running before measurement.

The persistent measurement log is:

```text
~/Library/Logs/MouseDebounce.measure.log
```

Reading the log does not affect the measurement process. `cat FILE` prints the current contents once; `tail -f FILE` follows it live. `mousedebouncectl measure` handles live following automatically.

Suggested measurement actions:

- left: normal clicks, double-clicks, short and long drags;
- right: same if practical;
- middle: several clicks if used;
- wheel: **>=1 s** smooth one-direction scrolling at roughly steady speed, then the opposite direction, plus ordinary scroll bursts.

At session end it prints per-button sample counts, Tukey-IQR outlier removal, mean/median/p90/range, conservative cluster analysis, and suggested settings.

Raw-event measurement alerts by sound when it sees a suspected button bounce and
prints that event's entire terminal line in bold (`--sound-volume 0` mutes it).
It calls the same debounce functions as filtering, with separate state and no
input suppression. A short press alone is only a candidate: the returning Down
within the hold window triggers the alert. Duplicate Downs also trigger alerts.
Short-press bounce-pair alerts are yellow as well as bold; duplicate-Down alerts
are bold only. The line also names the reason, including in a plain-text log.
These warnings mean “the filter would suppress this,” not proof of faulty hardware.
Measurement uses your saved timing and volume settings; pass timing options to
override them, or `--no-config` to try the defaults. `--debug` is not needed for
bounce alerts.
Pressing Ctrl-C ends the session cleanly and prints the summary collected so far.

If the mouse happens to behave perfectly during the session, the button recommendations may not contain useful chatter calibration data. Do **not** overfit settings to a clean session; rerun measurement when the fault recurs.

## Wheel-miss diagnostics

Version 0.6 still **does not synthesize missing wheel movement**. It only diagnoses likely missing pulses during locally stable discrete-wheel runs.

The detector keeps a short rolling history of same-direction inter-event gaps. Once local cadence is sufficiently stable, a gap close to an integer multiple (2x through 10x) of that cadence can be flagged, for example:

```text
WHEEL ... gap=83.2 ms ... <<< probable-V-miss=1 (local cadence 41.0 ms, ratio 2.03x)
```

False-positive defenses include:

- several recent gaps required before inference;
- median + MAD local cadence estimation;
- unstable cadence rejection;
- reset on direction changes;
- acceleration-reset cases downgraded to `possible-*`;
- no automatic wheel-event insertion.

This is intended for reasonably steady runs of roughly a second or more. Speed changes and pauses can still resemble missing pulses, so flagged lines are evidence rather than proof.

## App bundle and Accessibility

`make app` creates:

```text
build/MouseDebounce.app
```

The bundle is headless (`LSUIElement=true`) and requires only **Accessibility**. Input Monitoring is not required.

Install and grant:

```sh
tools/mousedebouncectl install
tools/mousedebouncectl grant
```

Then enable **Mouse Debounce** in System Settings -> Privacy & Security -> Accessibility.

### Rebuilt-app permission quirk

The development build is ad-hoc codesigned. After replacing the app with a newly rebuilt version, macOS may retain a stale Accessibility entry but fail to recognize the new binary. If MouseDebounce keeps asking for Accessibility despite already appearing in the list, reset that one approval and grant again:

```sh
tools/mousedebouncectl reset-accessibility
tools/mousedebouncectl grant
```

Equivalent manual workaround: remove the old **Mouse Debounce** entry from Accessibility, then grant the newly installed build.

`reset-accessibility` affects only the bundle ID `io.mouse-debounce.MouseDebounce`; it is not run automatically.

## Service control

```sh
tools/mousedebouncectl start
tools/mousedebouncectl stop
tools/mousedebouncectl restart
tools/mousedebouncectl status
tools/mousedebouncectl logs
```

`launchd` handles the normal long-running filter. Measurement is finite and tracked by the controller.

## Source map

| File | Purpose |
|---|---|
| `src/debounce_logic.*` | Pure debounce state machine |
| `src/timing_settings.*` | Per-button timing + sibling inheritance |
| `src/statistics.*` | IQR statistics and threshold heuristic |
| `src/wheel_analysis.*` | Portable local-cadence missing-pulse detector |
| `src/monotonic_clock.*` | macOS monotonic nanosecond receipt clock |
| `src/measurement.*` | Button/wheel tracing and recommendations |
| `src/debounce_filter.*` | CoreGraphics adapter/timers/withheld Ups |
| `src/mouse_button.*` | Portable button types/names |
| `src/mouse_events.*` | CoreGraphics event decoding/native timestamps |
| `src/config_file.*` | Tiny `config.args` reader/writer |
| `src/event_tap.*` | Minimal `CGEventTap` lifecycle |
| `src/permissions.*` | Accessibility permission request/check |
| `src/signal_bridge.*` | SIGINT/SIGTERM -> run-loop shutdown |
| `src/options.*` | CLI/config parsing |
| `src/main.c` | Wiring |
| `tools/mousedebouncectl` | install/service/measure/save wrapper |

## Build and tests

On macOS with Apple command-line developer tools:

```sh
make app
make test
```

Portable tests cover debounce state transitions, timing inheritance, CLI/config parsing and save/load behavior, IQR/threshold statistics, and missing-wheel-pulse cadence logic.

`make test-measurement` additionally checks measurement alerts and bold markers
with synthetic CoreGraphics events. It does not intercept input or play audio.

## Security / audit surface

The current source contains **no networking implementation** and no updater, plug-ins, telemetry, analytics, or shell-command execution from the C app. In particular, the C source does not call socket/connect/send/recv, `system`, `popen`, `fork`, `exec*`, `posix_spawn`, `dlopen`, or similar facilities.

The app itself:

- creates one CoreGraphics event tap;
- reads/writes only its explicit config, measurement output, and optional PID file;
- suppresses/reposts mouse-button events only as required by the debounce state machine;
- measurement observes events and writes text output.

The `mousedebouncectl` shell wrapper intentionally invokes standard local macOS utilities (`launchctl`, `open`, `tail`, `kill`, `tccutil`, file copy/removal) for installation/lifecycle management. It contains no network commands.

The repository contains no hard-coded user name, email address, `/Users/<name>` path, project path, or other personal identifier. Home-relative paths are derived from `$HOME`/the current account at runtime.

GPL-3.0-or-later, reflecting the GPL projects whose design ideas were intentionally reused:

- https://github.com/franzos/mouse-debounce
- https://github.com/vorssaint/vorssaint-utils
