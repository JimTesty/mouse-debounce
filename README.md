# Mouse Debounce for macOS

A small, source-auditable macOS utility for repairing worn mouse-button chatter in software.

Version **0.4.0** combines:

- the two-threshold state-machine idea from `franzos/mouse-debounce`;
- the useful macOS `CGEventTap` plumbing pattern from Vorssaint;
- per-button timing, measurement statistics/recommendations, persistent config, and optional launchd lifecycle control.

The filter handles **left, right, and middle buttons by default**. Wheel events are measured but never modified.

## Debounce algorithm

```text
Down -> pass immediately
short Up -> temporarily withhold
returning Down before hold timeout -> discard {Up, Down} as a bounce pair
otherwise -> release the withheld Up
```

Normal Down latency is unchanged. Only an Up that already looks suspicious is delayed.

Defaults are now:

```text
short-ms = 20
hold-ms  = 20
buttons  = left,right,middle
```

## Per-button timing and inheritance

Every button can have separate thresholds:

```text
--left-short-ms 18
--left-hold-ms 16
--right-short-ms 20
--right-hold-ms 22
--middle-short-ms 25
--middle-hold-ms 20
```

Global options still work:

```text
--short-ms 20 --hold-ms 20
```

A global option sets all three buttons. Later arguments win, so this is useful:

```text
--short-ms 20 --hold-ms 20 --middle-short-ms 30
```

If no global value was assigned and a button-specific value is missing, it inherits from explicitly set siblings:

```text
--left-short-ms 10 --right-short-ms 30
```

resolves to:

```text
left=10, right=30, middle=20
```

If only one sibling is set, the unset buttons copy it. If no button is set, the built-in fallback is 20 ms.

## Persistent config

The default config path is:

```text
~/Library/Application Support/MouseDebounce/config.args
```

This was chosen instead of `build/config.*` because rebuilds should never erase settings, and instead of writing a raw plist into `~/Library/Preferences` because a tiny human-readable argument file is easier to audit and edit.

Example:

```text
# MouseDebounce filter settings
--buttons left,right,middle
--short-ms 20
--hold-ms 20
--middle-short-ms 25
```

Syntax is intentionally simple: whitespace-separated arguments and `#` comments. Quoted arguments are not implemented because filter settings do not need them.

The config loads first; command-line arguments load afterward and therefore override it.

Useful options:

```text
--no-config
--config /some/other/config.args
--save-config
```

For example:

```sh
open -n build/MouseDebounce.app --args \
  --short-ms 20 --hold-ms 20 --middle-short-ms 25 --save-config
```

`--save-config` writes a canonical resolved config and then continues running.

## Measurement mode

Measurement records:

- Down -> Up press durations for left/right/middle;
- Up -> next Down gaps for each button;
- scroll-wheel timing/delta/direction data.

Example:

```sh
rm -f /tmp/mouse-debounce-measure.txt
open -n build/MouseDebounce.app --args \
  --measure --duration 60 --output /tmp/mouse-debounce-measure.txt

tail -f /tmp/mouse-debounce-measure.txt
```

At the end it prints basic statistics for each button and metric:

```text
n
IQR outliers removed
mean
median
90th percentile
range
```

For threshold recommendation it examines timings up to 250 ms, searches for a clearly separated low-timing cluster, removes Tukey-IQR outliers *within that cluster*, and puts the suggested threshold halfway between the cleaned low cluster and the next cluster.

Conceptually:

```text
bounce-ish timings     normal-ish timings
5  7  8  10             55  60  75  90
         ^                ^
       max low          min high
             \          /
           suggestion ~33 ms
```

This is a heuristic, not an oracle. Two genuine very-fast clicks can have the same waveform as switch chatter. If no convincing split exists, the tool refuses to invent a direct recommendation for that button; it instead uses the average of measured siblings, or 20 ms if none are available.

Include soft clicks, deliberate double-clicks, and drag-selects during measurement so the distributions contain the cases you care about.

## Wheel misses

A completely missing wheel detent cannot be reconstructed reliably:

```text
physical detent -> no observable event
```

Software cannot distinguish that from intentionally stopping the wheel. The measurement trace can still diagnose more recoverable faults such as isolated reverse-direction ticks or erratic deltas. Version 0.4 therefore does not synthesize wheel movement.

## App bundle and macOS privacy identity

`make app` creates:

```text
build/MouseDebounce.app
```

The bundle is headless (`LSUIElement=true`) but gives macOS a proper application identity.

**Mouse Debounce 0.4 requires only Accessibility permission.** Earlier versions unnecessarily requested both Input Monitoring and Accessibility. The filter needs an active CoreGraphics event tap because it sometimes suppresses a physical event and later posts a withheld Up. Measurement now deliberately uses that same active tap but returns every event unchanged. Because the app needs Accessibility for its normal job anyway, requesting a second, weaker Input Monitoring permission only for measurement adds complexity without improving practical security.

`tools/mousedebouncectl grant` launches the installed app so it can request Accessibility, then opens the Accessibility pane. macOS often suppresses repeat permission dialogs after a previous allow/deny decision; in that case simply enable **Mouse Debounce** in the pane.

The local build is ad-hoc signed. A stable Apple development signature is preferable if repeated rebuilds cause TCC to treat builds as different code identities.

## launchd lifecycle control

A native user LaunchAgent is cleaner than hunting PIDs with `ps` and `kill`.

Build and install:

```sh
make app
tools/mousedebouncectl install
```

Trigger/open the Accessibility permission flow once through LaunchServices:

```sh
tools/mousedebouncectl grant
```

Then:

```sh
tools/mousedebouncectl start
tools/mousedebouncectl status
tools/mousedebouncectl stop
tools/mousedebouncectl restart
tools/mousedebouncectl logs
tools/mousedebouncectl uninstall
```

The generated LaunchAgent includes `AssociatedBundleIdentifiers=io.mouse-debounce.MouseDebounce` so macOS has an explicit association between the LaunchAgent and the app bundle.

The service reads the normal config file, so changing thresholds does not require rewriting the LaunchAgent.

`uninstall` intentionally leaves the config file behind.

## Source map

| File | Purpose |
|---|---|
| `src/debounce_logic.*` | Pure debounce state machine |
| `src/timing_settings.*` | Per-button timing + sibling inheritance |
| `src/statistics.*` | Portable IQR statistics and cluster-split heuristic |
| `src/measurement.*` | Button/wheel measurement and recommendations |
| `src/debounce_filter.*` | CoreGraphics adapter, timers, withheld-Up storage |
| `src/mouse_button.*` | Platform-independent button names/types |
| `src/mouse_events.*` | CoreGraphics mouse-event decoding |
| `src/config_file.*` | Tiny config.args reader/writer |
| `src/event_tap.*` | Minimal `CGEventTap` lifecycle |
| `src/permissions.*` | Single Accessibility permission request/check |
| `src/signal_bridge.*` | SIGINT/SIGTERM -> run-loop shutdown |
| `src/options.*` | CLI/config parsing and precedence |
| `src/main.c` | Wiring only |
| `tools/mousedebouncectl` | launchd install/start/stop/status wrapper |

## Build and tests

Requires Apple's command-line developer tools on macOS:

```sh
make
```

Portable unit tests cover:

- debounce state transitions;
- timing inheritance/defaults;
- IQR removal and threshold-cluster detection.

Run only tests:

```sh
make test
```

## Safety / audit surface

The filtering process:

- creates one CoreGraphics event tap;
- observes/suppresses only left/right/middle Down/Up events;
- posts only a previously withheld Up;
- has no network code;
- launches no subprocesses;
- dynamically loads no plug-ins;
- reads only its explicit tiny config file;
- writes only an explicitly requested measurement output or config save.

The optional shell control script copies the app into `~/Applications`, writes/removes one user LaunchAgent plist, and calls Apple's `launchctl` / `open` commands.

The source remains GPL-3.0-or-later because it intentionally derives design ideas from GPL-3.0-or-later projects:

- https://github.com/franzos/mouse-debounce
- https://github.com/vorssaint/vorssaint-utils
