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
   hold or drag. For example, after a 993.6 ms press it reports Up, followed by
   Down 23.9 ms later.

In both cases, the problem is an unintended release followed by a quick return
to the pressed state. The filter repairs that pair as described below.

## Debounce algorithm

The main setting is `hold-ms`: how long to wait after an **Up** before delivering
the release to the application. A first **Down** is delivered immediately. If
another Down arrives before that wait ends, the filter discards the withheld Up
and returning Down. The application sees one uninterrupted press. Otherwise,
the Up is delivered when the wait ends. A Down at or after the deadline starts
a new press. Duplicate Downs while already held are also suppressed.
There is no debounce delay on Down events: they either pass immediately or are
discarded as part of a bounce pair or as duplicates. Only Up events are delayed.

`short-ms` is an optional restriction on which releases get that wait:

- **`short-ms=0` (default)** means no press-length limit, effectively infinity.
  Every Up following a press gets the `hold-ms` window, even after a long hold.
  The cost is an added `hold-ms` of latency on every genuine release following a
  press, even when the mouse is behaving perfectly.
- **`short-ms>0`** targets the first glitch pattern: chatter shortly after a Down.
  Only an Up arriving less than `short-ms` after the most recent physical Down
  gets the window. Releases after longer presses pass immediately.

Thus `short-ms=0` does **not** disable debouncing or set the release delay to zero.
`hold-ms` remains the release delay in either mode. An unmatched Up (for example,
when the utility starts with the button already held) passes through to avoid
leaving the application stuck in a pressed state.

For initial-press chatter, either mode can repair the pair, provided the Up gets
the hold window. For glitches during long holds, use `short-ms=0` and a `hold-ms`
longer than the glitch gap. `short-ms=50` misses the 993.6 ms example because the
preceding press lasted over 50 ms.

For the second case, with `--short-ms 0 --hold-ms 70`:

```text
   0 ms  Down -> delivered immediately; begin holding/dragging
1000 ms  Up   -> withheld until 1070 ms; application still sees button held
1024 ms  Down -> discard this Down and the withheld Up; drag continues
2000 ms  Up   -> actual release; withheld until 2070 ms
2070 ms      -> no Down returned, so deliver the release
```

Using positive `short-ms` is a latency tradeoff: repair chatter
after brief presses while leaving long-press releases immediate. It does not
cover intermittent contact during a long hold. Leave it at `0` for that fault.
Neither mode removes an isolated false Down: initial presses still pass through
immediately, and the filter cannot know your physical intent.

Defaults:

```text
short-ms = 0
hold-ms  = 25
buttons  = left,right,middle
```

Increasing `hold-ms` catches longer glitches but delays genuine releases more.
Intentional re-clicks whose Up-to-Down gap is shorter than this window can be
merged into one press. With `short-ms=0`, both short and long presses have this
tradeoff. The 25 ms default covers glitches under 25 ms, but not longer ones.
Choose a window based on your mouse's measurements, including intentional
double-clicks: their gaps can overlap glitch timings, so a longer window can
suppress genuine re-clicks too.

## Timing clock

MouseDebounce does **not** use `CGEventGetTimestamp()` for debounce or measurement intervals.

Each callback is timestamped with macOS `CLOCK_UPTIME_RAW`, which directly returns monotonic nanoseconds. This avoids assumptions about Quartz event-timestamp representation or Mach timebase conversion. A `20 ms` debounce setting therefore means approximately 20 ms of real elapsed callback-receipt time.

When a withheld Up is reposted, its Quartz timestamp is refreshed with a native CoreGraphics timestamp rather than converting the monotonic clock into Quartz units.

## Per-button timing and inheritance

Buttons can have separate values:

```text
--left-hold-ms 30
--right-hold-ms 25
--middle-hold-ms 20
```

Global options set all buttons:

```text
--short-ms 0 --hold-ms 70
```

Later arguments win. If a per-button value is unset and no global value supplied,
it inherits the arithmetic mean of explicitly configured sibling buttons. With
no configured siblings, the defaults are `short-ms=0` and `hold-ms=25`.
The `--left-short-ms`, `--right-short-ms`, and `--middle-short-ms` overrides still
exist. Explicit zero counts as a configured value in inheritance, not as “unset.”
Use global `--short-ms 0` to select unrestricted release repair for all buttons.

Filtering state and deadlines are independent per button. For example,
`--short-ms 0 --right-short-ms 50` repairs releases after any left/middle press,
but only after short right presses. A bounce or timeout on one button cannot
cancel or release another button's pending Up.

## Persistent config

Default path:

```text
~/Library/Application Support/MouseDebounce/config.args
```

The format is deliberately just app arguments plus optional `#` comments:

```text
--buttons left,right,middle
--short-ms 0
--hold-ms 25
--sound-volume 0.1
```

Config loads first and CLI arguments override it.

Existing saved positive `short-ms` values retain their old behavior; changing
the default does not override them. To repair long-hold glitches, explicitly
save `--short-ms 0` along with your chosen hold window.

`--sound-volume` accepts `0` through `1` and is saved like the other settings;
`0` silences all sounds. `--debug` enables startup and filter diagnostic sounds.
`--debug-wheel` independently enables a sound on each wheel-down event; it does
not require `--debug`. With neither switch, normal filtering is silent. Both
settings persist: use `tools/mousedebouncectl save --debug` to enable filter
diagnostics across restarts, and `tools/mousedebouncectl save --no-debug` to
disable them later. To disable saved
wheel sounds, remove the `--debug-wheel` line from the config and restart the
service. There is no `--no-debug-wheel`; `--no-debug` does not turn off wheel sounds.

Recommended save command:

```sh
tools/mousedebouncectl save --short-ms 0 --hold-ms 70
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
from the filter's actual decision: either an Up/Down pair within `hold-ms` or a
duplicate Down. The earlier Up remains in the log; it cannot be identified as
part of a pair until the returning Down arrives. These notes describe the
filter's classification, not proof of a hardware glitch.

Button entries end with elapsed time since that same button's previous raw event,
such as ` (45.67ms)`, before any glitch note. Other buttons and scrolling do not
reset this timer. A button's first event in each run has no elapsed time.

Each event starts with local date and time to hundredths of a second, such as
`2026-09-10 17:24:56.78`.
A `-----` line precedes a Down when more than one second has passed since the
previous logged event (button or wheel). Movement does not reset this interval.

`--log` is saved and has no effect in `measure` mode. Remove its line from the
config and restart the service to disable it. Logs are not rotated automatically;
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
input suppression. An Up alone is not an alert: the returning Down within the
hold window triggers it. With `short-ms=0`, this works after long holds too;
positive `short-ms` retains the press-length restriction. Duplicate Downs also
trigger alerts. Up/Down bounce-pair alerts are yellow as well as bold;
duplicate-Down alerts are bold only. The line also names the reason, including
in a plain-text log.
These warnings mean “the filter would suppress this,” not proof of faulty hardware.
Measurement uses your saved timing and volume settings; pass timing options to
override them, or `--no-config` to try the defaults. `--debug` is not needed for
bounce alerts.
Suggested settings estimate `hold-ms` from release-to-Down gaps and preserve your
configured `short-ms`; calibration does not silently switch the detection mode.

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
| `src/mouse_events.*` | CoreGraphics event decoding/native timestamps |
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
