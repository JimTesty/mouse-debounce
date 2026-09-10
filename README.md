# Mouse Debounce for macOS

A small, source-auditable macOS utility for repairing worn mouse-button chatter in software.

Version **0.6.0** provides:

- mouse-button release-bounce repair, including glitches during long holds;
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

## Two common glitch patterns

1. **Chatter around the initial press.** You press once, but the switch rapidly
   reports `Down -> Up -> Down`. The extra Up/Down pair can look like a second click.
2. **A brief release during a hold or drag.** You are still pressing, possibly
   lightly, but the switch momentarily reports Up and then Down, interrupting the
   hold or drag. For example, after roughly a one-second press it may report Up,
   followed by Down about 20 ms later.

In both cases, the problem is an unintended release followed by a quick return
to the pressed state. The filter repairs that pair as described below.

## Debounce algorithm

Every **Up** matched to a press is withheld for one of two release delays. The
filter compares its arrival time with the most recent physical **Down** for that
button:

- if the elapsed time is less than `short0-ms`, it uses `hold0-ms`;
- otherwise, it uses `hold-ms`.

`short0-ms=0` means the first condition can never match, so every matched Up uses
the normal `hold-ms` delay.

If another Down arrives before the selected deadline, the filter discards both
the withheld Up and the returning Down. The application sees one uninterrupted
press. Otherwise, the Up is delivered when the deadline expires, and a Down at
or after the deadline begins a new press. Every physical Down updates the time
used for the next Up, even when that Down was suppressed as the return half of a
bounce pair or as a duplicate. The delay is therefore selected independently
for every Up, not just the first Up of a click.

Down events are never delayed: they pass immediately or are suppressed as a
returning bounce or duplicate. An unmatched Up, such as one received after the
utility starts while a button is already held, passes immediately to avoid
leaving the application stuck in a pressed state. The filter cannot remove an
isolated false Down because it cannot know the user's physical intent.

For example, the defaults `--short0-ms 50 --hold0-ms 40 --hold-ms 25` catch a
32 ms return after a short press but let the same gap begin a new press after a
long hold:

```text
   0 ms  Down -> delivered immediately
  30 ms  Up   -> recent Down, withheld for 40 ms
  62 ms  Down -> discard this Down and the withheld Up; record this physical Down
 100 ms  Up   -> recent suppressed Down, withheld for 40 ms
 140 ms       -> no Down returned, so deliver the release
1000 ms  Down -> delivered immediately
2000 ms  Up   -> older Down, withheld for 25 ms
2025 ms       -> no Down returned, so deliver the release
2032 ms  Down -> delivered as a new press
```

Defaults:

```text
short0-ms = 50
hold0-ms  = 40
hold-ms   = 25
buttons   = left,right,middle
```

The defaults use a 40 ms delay for releases less than 50 ms after the latest
physical Down and preserve the 25 ms normal release delay for all other presses.
Increasing either release delay catches longer glitches but delays the genuine
releases that select it. Intentional re-clicks whose Up-to-Down gap is shorter
than the selected delay can be merged into one press. Choose the delays from
measurements that include intentional double-clicks, whose gaps can overlap
glitch timings.

## Timing clock

MouseDebounce does **not** use `CGEventGetTimestamp()` for debounce or measurement intervals.

Each callback is timestamped with macOS `CLOCK_UPTIME_RAW`, which directly returns monotonic nanoseconds. This avoids assumptions about Quartz event-timestamp representation or Mach timebase conversion. A `20 ms` debounce setting therefore means approximately 20 ms of real elapsed callback-receipt time.

When a delayed Up is reposted, its original event timestamp and mouse `(x, y)`
position are preserved. Only delivery is delayed; the event does not use the
pointer's newer position or the replay time.

## Per-button timing and inheritance

Buttons can have separate values:

```text
--left-short0-ms 50 --left-hold0-ms 70 --left-hold-ms 30
--right-short0-ms 40 --right-hold0-ms 60 --right-hold-ms 25
--middle-short0-ms 0 --middle-hold0-ms 70 --middle-hold-ms 20
```

Global options set all buttons:

```text
--short0-ms 50 --hold0-ms 70 --hold-ms 30
```

Later arguments win. If a per-button value is unset and no global value supplied,
it inherits the arithmetic mean of explicitly configured sibling buttons for
that same metric. With no configured siblings, the defaults are `short0-ms=50`,
`hold0-ms=40`, and `hold-ms=25`. Explicit zero counts as a configured value in
inheritance, not as “unset.”

The old `--short-ms`, `--left-short-ms`, `--right-short-ms`, and
`--middle-short-ms` names remain accepted as aliases for their `short0-ms`
counterparts. Saved configuration always uses the canonical `short0-ms` names.

Filtering state and deadlines are independent per button. For example,
`--short0-ms 0 --right-short0-ms 50` selects the normal hold window for every
left/middle release, while right releases can select either window. A bounce or
timeout on one button cannot cancel or release another button's pending Up.

## Persistent config

Default path:

```text
~/Library/Application Support/MouseDebounce/config.args
```

The format is deliberately just app arguments plus optional `#` comments:

```text
--buttons left,right,middle
--short0-ms 50
--hold0-ms 40
--hold-ms 25
--sound-volume 0.1
```

Config loads first and CLI arguments override it.

Existing configs using `short-ms` still load, but a positive value now selects
between `hold0-ms` and `hold-ms`. In particular, releases after longer presses
are now held for `hold-ms` instead of passing immediately. Saving rewrites the
alias as canonical `short0-ms`.

`--sound-volume` accepts `0` through `1` and is saved like the other settings;
`0` silences all sounds. `--debug` enables startup and filter diagnostic sounds.
`--debug-wheel` independently enables a sound on each wheel-down event; it does
not require `--debug`. With neither switch, normal filtering is silent. Both
settings persist: use `tools/mousedebouncectl save --debug` to enable filter
diagnostics across restarts, and `tools/mousedebouncectl save --no-debug` to
disable them later. Use `tools/mousedebouncectl save --no-debug-wheel` to disable
saved wheel sounds; `--no-debug` does not turn off wheel sounds. Disabled flags
are omitted from the saved config, rather than written as `--no-*` options.

Recommended save command:

```sh
tools/mousedebouncectl save --short0-ms 50 --hold0-ms 70 --hold-ms 30
```

This calls `--save-config-and-exit` and prints the saved config file, so no extra long-running process remains. If the launchd service was running, the controller restarts it so the new config takes effect.

View saved settings:

```sh
tools/mousedebouncectl config
```

## Event logging

Use `tools/mousedebouncectl save --log` to enable logging during normal filtering.
Button Down/Up events (including extra buttons) and scrolling are appended to
`events.log` in the config folder:

```text
~/Library/Application Support/MouseDebounce/events.log
```

With `--config PATH`, the log goes beside that config file. Movement and dragging
are not logged. All raw button events are logged, even for buttons not enabled
for filtering. Entries include suppressed events but exclude releases replayed
by the filter. A suppressed Down gets a same-line `<<< suspected bounce` note
from the filter's actual decision: either an Up/Down pair within the selected
hold window or a duplicate Down. The earlier Up remains in the log; it cannot be
identified as
part of a pair until the returning Down arrives. These notes describe the
filter's classification, not proof of a hardware glitch.

Button entries end with elapsed time since that same button's previous raw event,
such as ` (45.67ms)`, before any glitch note. Other buttons and scrolling do not
reset this timer. A button's first event in each run has no elapsed time.

Each event starts with local date and time to hundredths of a second, such as
`2026-09-10 17:24:56.78`.
A `-----` line precedes a Down when more than one second has passed since the
previous logged event (button or wheel). Movement does not reset this interval.
The first logged event in a run also gets a separator, even if it is scrolling.
Separators use the shared last-event timestamp, not per-button timestamps;
switching buttons does not by itself start a new group.

`--log` is saved and has no effect in `measure` mode. Use
`tools/mousedebouncectl save --no-log` to disable it. Logs are not rotated automatically;
for long sessions, check disk usage and remove unneeded logs after stopping the
service. Logging records input activity and adds file-writing overhead.

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
input suppression. An Up alone is not an alert: a returning Down within the
selected hold window triggers it. Pairs found through either `hold0-ms` or
`hold-ms` are yellow as well as bold. Duplicate Downs also trigger alerts and
are bold only. The line names the reason in both the terminal and plain-text log.
These warnings mean “the filter would suppress this,” not proof of faulty hardware.
Measurement uses your saved timing and volume settings; pass timing options to
override them, or `--no-config` to try the defaults. `--debug` is not needed for
bounce alerts.
Suggested settings estimate `hold-ms` from release-to-Down gaps and preserve
your configured `short0-ms` and `hold0-ms`; calibration does not silently change
the two-window selection rule.

TODO: Rework the settings-suggestion algorithm. Timing clusters alone cannot
reliably distinguish real re-clicks from switch glitches, and a clean session
does not establish a safe debounce window. Consider guided, user-labelled tests
and withholding recommendations when the evidence is insufficient. Until then,
treat the suggested values as experimental, not calibrated settings.
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
| `src/mouse_events.*` | CoreGraphics event decoding |
| `src/config_file.*` | Tiny `config.args` reader/writer |
| `src/event_tap.*` | Minimal `CGEventTap` lifecycle |
| `src/event_log.*` | Append-only raw button/wheel logging with local timestamps |
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
`make test-event-log` checks log formatting, idle separators, and appending using
synthetic events and temporary files, without accessing your saved config.

## Security / audit surface

The current source contains **no networking implementation** and no updater, plug-ins, telemetry, analytics, or shell-command execution from the C app. In particular, the C source does not call socket/connect/send/recv, `system`, `popen`, `fork`, `exec*`, `posix_spawn`, `dlopen`, or similar facilities.

The app itself:

- creates one CoreGraphics event tap;
- reads/writes only its explicit config, measurement output, optional event log, and optional PID file;
- suppresses/reposts mouse-button events only as required by the debounce state machine;
- measurement observes events and writes text output.

The `mousedebouncectl` shell wrapper intentionally invokes standard local macOS utilities (`launchctl`, `open`, `tail`, `kill`, `tccutil`, file copy/removal) for installation/lifecycle management. It contains no network commands.

The repository contains no hard-coded user name, email address, `/Users/<name>` path, project path, or other personal identifier. Home-relative paths are derived from `$HOME`/the current account at runtime.

GPL-3.0-or-later, reflecting the GPL projects whose design ideas were intentionally reused:

- https://github.com/franzos/mouse-debounce
- https://github.com/vorssaint/vorssaint-utils
