# mac-mouse-debounce

A tiny macOS CLI for measuring and filtering worn-microswitch chatter.

It combines:

- the two-threshold debounce rule from [`franzos/mouse-debounce`](https://github.com/franzos/mouse-debounce), and
- the useful minimum of Vorssaint's macOS approach: a HID-level, head-insert `CGEventTap`, fail-open handling, and event-tap recovery.

The program deliberately has **no network code, file I/O, subprocess execution, updater, analytics, configuration database, or third-party dependency**. The source is a single C file.

## Algorithm

For an enabled button:

1. `Down` passes immediately.
2. If `Up` arrives less than `--short-ms` later, hold that `Up` for `--hold-ms`.
3. If another `Down` arrives during the hold, discard the held `Up` and this new `Down` together. Downstream sees one continuous hold.
4. Otherwise emit the held `Up` when the timer expires.
5. Ordinary presses longer than `--short-ms` have no added latency.

The filter also flushes a withheld `Up` before normal SIGINT/SIGTERM exit and before resetting after a disabled event tap. A hard process crash/SIGKILL during the short withheld-Up window cannot be repaired by any user-space zero-Down-latency implementation.

## Build

Requires Apple's command-line developer tools (`clang`).

```sh
make
```

This uses only macOS system frameworks:

- `ApplicationServices`
- `CoreFoundation`

## Measure first

```sh
./mouse-debounce --measure
```

By default it measures only the left button. To include more:

```sh
./mouse-debounce --measure --buttons left,right,middle
```

It prints each Down/Up transition, press duration, release-to-next-Down gap, and macOS click-state. Ctrl-C prints a sorted summary of timings up to 300 ms. Measurement occurs at the same `CGHIDEventTap` layer the filter uses; it is not a raw `IOHIDManager` packet sniffer, but it measures exactly the events this filter can observe and suppress.

Record at least:

- ordinary clicks,
- deliberately fast clicks/double-clicks,
- soft clicks that tend to chatter,
- some drag-selects.

Choose:

- `--short-ms` above the longest **bounce press→release** duration, but below the shortest genuine press you want preserved;
- `--hold-ms` above the longest **bounce release→returning-Down** gap, but below your deliberate double-click gap.

The Linux author's mouse happened to use `80` / `70` ms. Do not assume those are correct for yours.

## Filter

Example:

```sh
./mouse-debounce --filter --short-ms 80 --hold-ms 70
```

Right/middle can be included explicitly:

```sh
./mouse-debounce --filter --buttons left,right,middle --short-ms 80 --hold-ms 70
```

## macOS permission

An active HID event tap requires macOS privacy permission. If creation fails, macOS may require the executable, or the Terminal application launching it, under **Privacy & Security → Accessibility** and/or **Input Monitoring** (wording/location varies by macOS version).

`--measure` uses a listen-only tap; `--filter` uses an active filtering tap.

## Safety / audit notes

The source does only the following externally visible things:

- creates one `CGEventTap` for mouse Down/Up events;
- observes or suppresses those events;
- posts only a previously withheld mouse-Up event, tagged so it is not re-filtered;
- writes timing/status text to stdout/stderr;
- uses a local pipe solely to convert SIGINT/SIGTERM into a safe run-loop shutdown.

No filesystem, network, dynamic loading, shell execution, or IPC to other programs is used.

### Why a stuck-button window still exists

During the `--hold-ms` interval, downstream has already received `Down` but has not yet received the suspicious `Up`. If the process is forcibly killed at exactly that time, it cannot emit the owed `Up`. Discarding the bounce `Up + Down` in pairs fixes logical pairing **during execution**, but cannot make an already-delivered Down atomic with a future event. Avoiding that window completely would require delaying the original Down too, adding click-down latency.

Normal Ctrl-C/SIGTERM and event-tap disable paths explicitly flush pending Ups first.

## Attribution / license

This implementation intentionally follows ideas/source architecture from two GPL-3.0-or-later projects:

- https://github.com/franzos/mouse-debounce
- https://github.com/vorssaint/vorssaint-utils

Accordingly this package is released under **GPL-3.0-or-later**.
