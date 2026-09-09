# Mouse Debounce for macOS

A small, source-auditable macOS mouse-switch repair utility.

It combines:

- the two-threshold state-machine idea from `franzos/mouse-debounce`, and
- the useful macOS plumbing pattern from Vorssaint: a HID-level `CGEventTap`,
  safe tap reset/re-enable, and fail-open handling.

Version 0.2 is deliberately modular. The debounce policy is isolated from the
macOS event plumbing and has platform-independent unit tests.

## Source map

| File | Purpose |
|---|---|
| `src/debounce_logic.*` | Pure debounce state machine; no macOS APIs |
| `src/debounce_filter.*` | CoreGraphics adapter, withheld-Up storage and timers |
| `src/mouse_events.*` | Left/right/middle event decoding |
| `src/measurement.*` | Button + wheel timing measurement |
| `src/event_tap.*` | Minimal `CGEventTap` lifecycle wrapper |
| `src/permissions.*` | Requests ListenEvent/PostEvent privacy access |
| `src/signal_bridge.*` | Safe SIGINT/SIGTERM -> run-loop shutdown |
| `src/options.*` | CLI parsing/defaults |
| `src/main.c` | Wiring only |

## Defaults

Filtering is the default mode, and **left, right, and middle are all enabled by default**:

```text
Down -> pass immediately
short Up -> temporarily withhold
returning Down before hold timeout -> discard {Up, Down} as a bounce pair
otherwise -> release the withheld Up
```

Defaults are currently:

- `--short-ms 80`
- `--hold-ms 70`
- `--buttons left,right,middle`

Measure your own mouse before trusting those thresholds.

## Why an `.app` bundle?

macOS privacy/TCC decisions are associated with *responsible code*. A bare
command-line binary launched by Terminal may therefore cause **Terminal** to be
shown in Input Monitoring / Accessibility.

`make` instead creates:

```text
build/MouseDebounce.app
```

with a stable bundle identifier and an ad-hoc code signature. Launch the app via
Finder or `open` so macOS has a real app identity to associate with the privacy
request:

```sh
make
open -n "build/MouseDebounce.app"
```

The application is `LSUIElement`, so it has no Dock icon and no GUI baggage.
Launching it with no arguments starts filtering.

The code calls CoreGraphics' ListenEvent and PostEvent preflight/request APIs.
Depending on macOS version and existing TCC state, System Settings may use the
labels **Input Monitoring** and/or **Accessibility**.

An app bundle is the correct structure, but recent macOS versions have had some
quirks around when an app appears in the Input Monitoring list. Packaging cannot
force TCC to list an app if the OS itself declines to register the request.

### Code signing

The Makefile uses an **ad-hoc signature** (`codesign -s -`) for a locally built
copy. If you later want a stable distributable build, sign it with your own Apple
Developer certificate instead. TCC may ask again when code identity changes.

## Measure mode

Measurement is listen-only and includes:

- left button Down/Up timing,
- right button Down/Up timing,
- middle button Down/Up timing,
- scroll-wheel deltas/direction/timing.

If launched directly from a shell:

```sh
"build/MouseDebounce.app/Contents/MacOS/MouseDebounce" --measure
```

For the strongest chance that TCC attributes the request to **Mouse Debounce**
rather than Terminal, launch it through LaunchServices and explicitly write the
measurement to a chosen file:

```sh
rm -f /tmp/mouse-debounce-measure.txt
open -n "build/MouseDebounce.app" --args \
  --measure --duration 30 --output /tmp/mouse-debounce-measure.txt

tail -f /tmp/mouse-debounce-measure.txt
```

After 30 seconds the app exits and writes the summary. Change `--duration` as
needed. `--output` is the only intentional ordinary-file write in the program.

## Wheel misses

A true missed wheel detent is fundamentally different from switch bounce:

```text
physical detent happened -> OS received no event
```

There is no observation from which software can reliably infer that missing
movement. Inventing a tick based on timing would produce false scrolls whenever
you intentionally pause.

This version therefore **measures but does not modify wheel events**. The trace
can still diagnose a more repairable failure mode: an isolated wrong-direction
pulse inside a run of same-direction scrolling. That could later be an optional
filter, but it should not be enabled blindly.

## Build and tests

Requires Apple's command-line developer tools.

```sh
make
```

`make` also runs platform-independent tests of `debounce_logic.c`.

To run only the tests:

```sh
make test
```

## Useful commands

Start filtering with defaults:

```sh
open -n "build/MouseDebounce.app"
```

Custom thresholds:

```sh
open -n "build/MouseDebounce.app" --args --filter --short-ms 60 --hold-ms 50
```

Only selected buttons:

```sh
open -n "build/MouseDebounce.app" --args --buttons left,right
```

Stop a headless instance:

```sh
killall MouseDebounce
```

SIGTERM is handled cleanly: any withheld mouse-Up is emitted before shutdown.
A hard crash/SIGKILL during the short withheld-Up interval remains inherently
unrecoverable without also delaying the original Down.

## Safety / audit surface

The filtering path:

- creates one CoreGraphics event tap;
- observes/suppresses only left/right/middle Down/Up events;
- posts only a previously withheld Up;
- has no network access;
- launches no subprocesses;
- dynamically loads no plug-ins;
- reads no user files;
- writes no ordinary files unless `--output PATH` was explicitly supplied for measurement.

The source is GPL-3.0-or-later because it intentionally derives design ideas from
GPL-3.0-or-later projects:

- https://github.com/franzos/mouse-debounce
- https://github.com/vorssaint/vorssaint-utils
