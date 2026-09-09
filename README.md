# Mouse Debounce for macOS

A small, source-auditable macOS utility for repairing worn mouse-button chatter in software.

Version **0.5.0** provides:

- Linux `franzos/mouse-debounce`-style short-release repair;
- minimal CoreGraphics event-tap plumbing inspired by Vorssaint;
- left/right/middle filtering by default;
- per-button timing inheritance;
- live measurement with statistics/recommendations;
- conservative missing-wheel-pulse diagnostics;
- a human-readable persistent config;
- launchd lifecycle control.

## Debounce algorithm

```text
Down -> pass immediately
short Up -> temporarily withhold
returning Down before hold timeout -> discard {Up, Down} as a bounce pair
otherwise -> release the withheld Up
```

Defaults:

```text
short-ms = 20
hold-ms  = 20
buttons  = left,right,middle
```

## Timing clock

Version 0.5 deliberately **does not use `CGEventGetTimestamp()` for debounce or measurement intervals**.

Each event is timestamped when the event-tap callback receives it using macOS `CLOCK_UPTIME_RAW`, which directly returns nanoseconds. This avoids assuming anything about Quartz event-timestamp representation or CPU-specific Mach tick conversion.

The <=20 ms debounce decision therefore means actual monotonic elapsed milliseconds at callback receipt. Event-tap scheduling jitter is normally tiny relative to the debounce window and is preferable to depending on undocumented/ambiguous timestamp behavior.

When a withheld Up is reposted, its Quartz timestamp is refreshed by asking CoreGraphics for a current native event timestamp rather than converting the monotonic clock into Quartz units.

## Per-button timing and inheritance

All buttons can have separate values:

```text
--left-short-ms 18 --left-hold-ms 16
--right-short-ms 20 --right-hold-ms 22
--middle-short-ms 25 --middle-hold-ms 20
```

Global options set all buttons:

```text
--short-ms 20 --hold-ms 20
```

Later arguments win. If a per-button value is unset and no global value supplied it inherits the arithmetic mean of explicitly configured sibling buttons. With no configured siblings it uses the 20 ms default.

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
```

Config loads first and CLI arguments override it.

Save settings and exit immediately:

```sh
MouseDebounce --short-ms 20 --hold-ms 20 --save-config-and-exit
```

The recommended installed-app interface is:

```sh
tools/mousedebouncectl save --short-ms 20 --hold-ms 20
```

If the launchd service was already running, `save` restarts it automatically so the new config takes effect. It does not leave another MouseDebounce process running.

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

1. pauses the normal debounce service if it is running, so measurement sees raw mouse events;
2. launches the signed `.app` through LaunchServices;
3. mirrors the measurement log to the terminal live;
4. auto-exits after the requested duration;
5. restores the debounce service if it had been running.

The persistent measurement log is:

```text
~/Library/Logs/MouseDebounce.measure.log
```

`cat`-ing a measurement file is harmless: it only reads the file and cannot terminate the app. `cat` prints the current contents once and exits. For live viewing use `tail -f`, which is what `mousedebouncectl measure` does internally.

At measurement start the app prints a suggested test procedure. Roughly:

- left: normal clicks, double-clicks, short/long drags;
- right: same;
- middle: several clicks if used;
- wheel: >=5 s smooth one-direction scrolling at roughly steady speed, then the opposite direction, plus ordinary scroll bursts.

At session end it prints per-button:

- sample count;
- Tukey-IQR outliers removed;
- mean, median, p90 and range;
- conservative low/high cluster analysis;
- suggested per-button settings and exact config/CLI arguments.

## Wheel-miss diagnostics

Version 0.5 still **does not synthesize missing wheel movement**. It now attempts to identify likely misses during stable discrete-wheel runs.

The detector keeps a short rolling history of same-direction inter-event gaps. Once the local cadence is sufficiently stable, a new gap close to an integer multiple (2x through 10x) of that cadence is flagged:

```text
WHEEL ... gap=83.2 ms ... <<< probable-V-miss=1 (local cadence 41.0 ms, ratio 2.03x)
```

False-positive defenses include:

- requiring at least several recent gaps;
- median + MAD local cadence estimation;
- rejecting unstable cadence;
- resetting on direction changes;
- labeling a same-direction acceleration reset (e.g. magnitude 5 -> 1) as lower-confidence, because it can be either a new gesture or a long miss that reset acceleration;
- refusing to repair anything automatically.

This deliberately targets long smooth scrolling, where a missing pulse is actually inferable. `probable-*` flags preserve the local acceleration state; `possible-*` flags coincide with an acceleration reset and are weaker evidence. A pause or speed change can still resemble a miss, so flagged lines are evidence to inspect rather than proof.

If the detector performs well on real logs, automatic insertion can be added later as a separate opt-in feature.

## App bundle and privacy

`make app` creates:

```text
build/MouseDebounce.app
```

The bundle is headless (`LSUIElement=true`) and requires only **Accessibility**. Input Monitoring is not required.

Grant once:

```sh
tools/mousedebouncectl install
tools/mousedebouncectl grant
```

Then enable **Mouse Debounce** in System Settings -> Privacy & Security -> Accessibility.

## Service control

```sh
tools/mousedebouncectl start
tools/mousedebouncectl stop
tools/mousedebouncectl restart
tools/mousedebouncectl status
tools/mousedebouncectl logs
```

`launchd` handles the normal long-running filter. Measurement is a finite, tracked LaunchServices session rather than another permanent service.

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

## Build/tests

On macOS with Apple command-line developer tools:

```sh
make
```

Portable tests cover:

- debounce state transitions;
- timing inheritance;
- IQR/threshold statistics;
- missing-wheel-pulse cadence logic.

This Linux build environment can execute those portable tests, but cannot link the actual macOS CoreGraphics app.

## Audit surface

The filter has no network code, plug-ins or updater. It creates one CoreGraphics event tap, reads its tiny explicit config, suppresses mouse-button events only when required by the state machine, and may repost only a previously withheld Up. Measurement writes only its requested log. `mousedebouncectl` uses ordinary macOS `launchctl`, `open`, `tail` and filesystem operations described above.

GPL-3.0-or-later, reflecting the GPL projects whose design ideas were intentionally reused:

- https://github.com/franzos/mouse-debounce
- https://github.com/vorssaint/vorssaint-utils
