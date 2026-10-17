# Stage 3 — Touch input research

Archived research record. The standalone `touch_probe` utility and its Make
target were removed; the gameplay application now owns touchscreen input.

Status: the standalone `touch_probe` utility was implemented, compiled locally,
and verified on the physical PW5. The user confirmed the corner
calibration, event handling, coexistence with the Kindle interface, and clean
shutdown checks described below.

## Scope and evidence

InkBoy remains an independent C application. Gambatte-K2 was inspected as a
technical reference only; its emulator, UI, and input code are not part of
InkBoy.

The repository evidence comes from the [Gambatte-K2
README](https://github.com/crazy-electron/gambatte-k2/blob/main/README.md),
[`gamejoypad.c`](https://github.com/crazy-electron/gambatte-k2/blob/main/gamejoypad.c),
and [`fbink.c`](https://github.com/crazy-electron/gambatte-k2/blob/main/fbink.c).
The Linux input behavior is described by the kernel documentation for the
[evdev interface](https://docs.kernel.org/input/input.html), [input event
codes](https://kernel.org/doc/html/v5.19/input/event-codes.html), and the
[multi-touch protocol](https://docs.kernel.org/5.19/input/multi-touch-protocol.html).

The original research phase did not have an SSH capture from the target PW5,
so device number, device name, permissions, axis ranges, protocol variant,
and orientation were initially hardware-verification items. A later probe run
on the physical PW5 supplied the device metadata and event output recorded in
the [physical PW5 evidence](#initial-physical-pw5-evidence) section below.

## Findings from Gambatte-K2

### Input mechanism

Gambatte-K2 has two separate concerns:

- Its graphical frontend uses GTK2/X11/XCB.
- Its virtual gamepad reads touchscreen events directly from a Linux evdev
  node using `linux/input.h`, `open()`, `poll()`, and `read()`.

Touch is not received through GTK signals, X11 events, or a Kindle-specific
widget abstraction. The worker opens the selected node read-only and
non-blocking, polls it with a 50 ms timeout, and drains complete
`struct input_event` records.

### Device discovery

The reference's `fb_ink_detect_touch_device()` calls FBInk's input scanner
with `TOUCHSCREEN | SCALED_TABLET` as the match mask. It uses the path of the
first matched device, but initializes its result to `/dev/input/event1`.
That fallback is an implementation detail of Gambatte-K2, not evidence that
`event1` is the touchscreen on a PW5. InkBoy must never hard-code an event
number without inspecting the target.

The scanner returns FBInk input-device metadata, including a path and a
matched flag. This is a discovery convenience, not a different input
transport: the subsequent read path is still Linux evdev.

### Event handling

The reference keeps two touch slots and handles the common type-B multi-touch
events as follows:

| Event | Gambatte-K2 behavior | Meaning for InkBoy |
| --- | --- | --- |
| `EV_ABS / ABS_MT_SLOT` | Selects the current slot | Select the contact state to update |
| `EV_ABS / ABS_MT_TRACKING_ID` | Non-negative marks a slot active; `-1` marks it inactive | Press/release for a type-B contact |
| `EV_ABS / ABS_MT_POSITION_X` | Stores the current slot's raw X | Update position |
| `EV_ABS / ABS_MT_POSITION_Y` | Stores the current slot's raw Y | Update position |
| `EV_SYN` | Evaluates active slots and button hit tests | Commit one input packet at `SYN_REPORT` |
| `EV_KEY / BTN_TOUCH` | Clears both slots when its value is zero | A compatibility release signal, not sufficient for all MT devices |

It also treats `BTN_TOOL_DOUBLETAP` release as a special case. The reference
does not implement a general axis-query or event-overflow recovery path, and
it limits itself to two slots. Those choices are acceptable observations for
the research, but should not become assumptions in the InkBoy parser.

The Linux input protocol is packetized: a hardware state change can produce
several events followed by `EV_SYN/SYN_REPORT`. A parser should update a
temporary contact state as records arrive and publish press, move, or release
only at the synchronization boundary. `SYN_DROPPED` must cause the parser to
discard the partial packet and resynchronize by querying the device state.

### Coordinate mapping in the reference

Gambatte-K2 stores the touchscreen's raw X/Y values without querying
`EVIOCGABS()` or reading the axis minimum and maximum. On each synchronized
packet it subtracts the GTK drawing-area window origin from the raw values.
Button centers and sizes are expressed as percentages of the GTK drawing-area
width and height. In effect, it assumes that the touchscreen's raw coordinate
space is already the same global display space used by X11.

That can work on the reference's tested setup, but it is not a portable PW5
calibration method. It does not establish the PW5 axis ranges, account for
touch/display offsets, or explicitly handle rotation. InkBoy should retain
the useful idea of testing a contact against a screen-space rectangle, while
making the raw-to-screen transform explicit and device-derived.

### Shutdown behavior

The reference uses a run-state flag and joins its worker thread when stopping.
The polling timeout gives the worker an opportunity to observe the stopped
state. InkBoy should use the same general lifecycle shape, but with explicit
ownership of the file descriptor: stop the loop, join the worker if one is
used, close the evdev descriptor, and free all associated state. A finite poll
timeout or a dedicated wakeup descriptor is preferable to an uninterruptible
blocking read.

The reference also attempts to set a real-time `SCHED_RR` priority. InkBoy
does not need that privilege or complexity for button input and should not
adopt it.

## Kindle touchscreen architecture

The expected Linux path is:

```text
touch controller/driver
        -> Linux input subsystem
        -> evdev character device
        -> /dev/input/eventX
        -> InkBoy read-only parser
```

`/dev/input/eventX` is a generic kernel interface. `X` is assigned by the
running system and is not a stable identity. Other nodes may represent keys,
power, page buttons, sensors, or other input sources. The touchscreen should
be selected by capabilities and device metadata, not by a fixed number.

The kernel exposes the device name and capabilities through `/sys/class/input`
and evdev ioctls. Useful checks for a future probe include:

- `EVIOCGNAME` for the human-readable device name;
- `EVIOCGBIT` to test for `EV_ABS`, `ABS_MT_POSITION_X`,
  `ABS_MT_POSITION_Y`, and preferably `ABS_MT_SLOT`;
- `EVIOCGABS(ABS_MT_POSITION_X/Y)` for minimum and maximum raw values;
- the `INPUT_PROP_DIRECT` property where available, which indicates a
  display-aligned direct device rather than a relative touchpad.

The actual PW5 firmware may expose a type-B multi-touch device, a
single-touch-compatible device, or both sets of events. This must be measured
on firmware 5.19.2. The parser should support the observed protocol rather
than presuming a particular controller or event node.

## Press, movement, and release model

For a type-B multi-touch device, the minimal state machine is:

1. On `ABS_MT_SLOT`, select a bounded slot state.
2. On a non-negative `ABS_MT_TRACKING_ID`, mark that slot pressed and retain
   its identifier.
3. On `ABS_MT_POSITION_X/Y`, update that slot's raw coordinates.
4. On `ABS_MT_TRACKING_ID = -1`, mark that slot released.
5. On `SYN_REPORT`, emit the complete set of changed contacts and their
   current screen coordinates.

For a device that exposes only the single-touch protocol, use `ABS_X` and
`ABS_Y` with `BTN_TOUCH` press/release events, committed at `SYN_REPORT`.
Supporting both forms costs little and avoids coupling the first POC to one
driver's reporting style.

The first InkBoy implementation only needs one contact for a `START` button.
The internal representation should nevertheless retain a small slot table so
that a later virtual-control screen can add two-finger input without changing
the evdev reader's ownership and cleanup model.

## Coordinate scaling and orientation

Raw touch coordinates are not guaranteed to be screen pixels. For each axis,
obtain the reported range `[minimum, maximum]` and normalize with bounded
integer arithmetic:

```text
screen_x = clamp((raw_x - minimum_x) * (screen_width  - 1)
                 / (maximum_x - minimum_x), 0, screen_width  - 1)
screen_y = clamp((raw_y - minimum_y) * (screen_height - 1)
                 / (maximum_y - minimum_y), 0, screen_height - 1)
```

The implementation must handle a zero or invalid range, reversed axes, and
values outside the advertised range. It should record the raw and normalized
coordinates during the POC so calibration errors are visible.

Orientation must be treated as a separate, explicit transform. The renderer's
logical display width and height come from FBInk's current display state; they
must not be inferred from a touchscreen event number or from the host build.
For a known quarter-turn, apply the corresponding axis swap and inversion
after normalization. For example, for a logical rectangle with width `W` and
height `H`:

```text
0°:   (x, y) -> (x,       y)
180°: (x, y) -> (W-1-x,   H-1-y)
90°:  (x, y) -> (H-1-y,   x)       # output dimensions H × W
270°: (x, y) -> (y,       W-1-x)    # output dimensions H × W
```

These formulas describe the chosen application convention; they are not a
claim about the PW5's current touch orientation. Corner touches on the
physical device are required to determine whether the driver already applies
rotation, whether X/Y are swapped, and whether there is an offset or dead
border. The PW5 framebuffer's known `viewVertOrigin=8` is rendering metadata
from Stage 2 and must not be reused as a touch offset without evidence.

## Approach comparison

| Concern | A — direct Linux evdev | B — existing abstraction or GTK input |
| --- | --- | --- |
| Dependencies | Linux headers and libc only | GTK2/X11, GLib, or an undocumented Kindle/KOReader library |
| Device selection | Explicit capability and axis inspection | FBInk helper may scan, but API availability and ABI must be verified |
| Event fidelity | Direct raw press, movement, release, MT slots, and sync packets | Depends on abstraction; GTK may hide raw ranges and protocol details |
| Current renderer fit | Fits the direct FBInk application without adding a window | GTK requires a window/event loop; KOReader libraries add runtime coupling |
| CPU/memory | One descriptor, `poll()`, and a small slot table | More framework state; still likely acceptable but unnecessary |
| PW5 risk | Requires permissions and target calibration | Library ABI, version, and device behavior are not established |
| Safe coexistence | Read-only open; do not use `EVIOCGRAB` | Framework may own focus or alter event routing |
| Future controls | Raw normalized contacts can feed buttons and gestures | Convenient only if the framework remains part of the application |

The repository's Stage 2 hardware observations found
`/mnt/us/koreader/libs/libfbink_input.so.1`, but not a linkable core
`libfbink.so`. That input helper is not an established InkBoy API or a
firmware contract. The current minimal vendored FBInk build is for rendering;
adding its input scanner would increase coupling and must not be assumed to
solve PW5 discovery.

## Recommendation

Use direct Linux evdev for InkBoy's first input implementation, with a small
read-only C module and no GTK2, X11, or KOReader runtime dependency.

The recommended design is:

- enumerate `/dev/input/event*` candidates and inspect names, capabilities,
  properties, and absolute-axis ranges;
- select a touchscreen candidate only after capability checks, failing with a
  diagnostic if selection is ambiguous;
- open it with `O_RDONLY | O_NONBLOCK` and never call `EVIOCGRAB`;
- poll with a timeout, read whole `struct input_event` records, maintain one
  or a small number of type-B slots, and commit state on `SYN_REPORT`;
- support the observed single-touch fallback (`ABS_X/Y` plus `BTN_TOUCH`) if
  the PW5 reports it;
- normalize raw coordinates from queried ranges, then apply a documented
  orientation transform and clamp to the FBInk display geometry;
- report `press`, `move`, and `release` to the application without drawing or
  refreshing the framebuffer from the input reader; and
- stop on `SIGINT`/`SIGTERM` through a flag or wakeup mechanism, join any
  worker, close the descriptor, and free state on every normal and error path.

This keeps input independent from the existing FBInk rendering path. It also
avoids changing Kindle system state: an ordinary read-only evdev client
receives events alongside the system's normal consumers. The application
must still be tested for permissions and coexistence on the target.

## Risks and limitations

- The tested PW5 selected `/dev/input/event1` named `pt_mt`; event numbering
  and names may differ on another device or firmware state.
- The application may lack permission to open the selected node when launched
  through the intended KPM/SSH workflow.
- The device may report type A, type B, single-touch compatibility events, or
  a vendor-specific combination; the first parser must log the observed
  capabilities and handle packet boundaries correctly.
- The tested ranges are `X 0..1235` and `Y 0..1647`, but corner calibration,
  swapped axes, inversion, display offsets, and rotation are not yet fully
  recorded.
- `SYN_DROPPED` and short/error reads must be handled so stale contact state
  does not become a stuck button.
- Opening the wrong event node could produce keys or power events. Capability
  filtering and human-readable diagnostics are required before automatic
  selection.
- Using `EVIOCGRAB` could prevent the Kindle interface from receiving touch;
  InkBoy must not grab the device for this milestone.
- A process killed with `SIGKILL`, stopped by power loss, or terminated by a
  system failure cannot run cleanup code. The POC should therefore avoid
  changing device state so its only essential cleanup is closing its own fd.
- E-ink rendering and input are independent timing domains. Touch events must
  not cause an implicit refresh loop or alter the Stage 2 save/restore logic.
- Gambatte-K2's two-slot limit, fallback event path, raw-coordinate assumption,
  and real-time scheduling choice are reference-project behaviors, not PW5
  requirements.

## Minimal proof-of-concept plan

The standalone POC was implemented in `tools/touch_probe.c` and has since been
removed; gameplay input is implemented in `src/input.c`.
It is separate from `src/main.c`, does not link FBInk or GTK, and uses only
Linux input headers plus libc. The Makefile builds it as `build/touch_probe`
for the Kindle target and `build/touch_probe-host` for a host smoke build.
`bin/deploy` transfers both executables to `/mnt/us/inkboy`; `bin/deploy
--probe` runs the probe remotely while `bin/deploy --run` retains the existing
InkBoy behavior.

Implementation details:

- Enumerates `/dev/input/event*`, opens each candidate read-only and
  non-blocking, and filters by `EV_ABS`, touch axes, `BTN_TOUCH`, and MT slot
  capabilities. It records `INPUT_PROP_DIRECT` when available, never assumes
  an event number, and closes every discovery descriptor.
- Prints each candidate's path, name, protocol classification, direct-device
  property, and reported X/Y ranges. With one candidate it selects it
  automatically; with multiple candidates it requires `--device` after the
  printed capability review.
- Parses type-B `ABS_MT_SLOT`, `ABS_MT_TRACKING_ID`, and MT position events,
  with a single-touch/type-A-compatible fallback where the device exposes
  only one contact stream. Events are committed at `SYN_REPORT`; `SYN_DROPPED`
  resets state and emits `TOUCH_CANCEL`.
- Normalizes raw values using `EVIOCGABS` ranges. The verified Stage 2 display
  geometry defaults are 1236×1648; `--width`, `--height`, and
  `--orientation 0|90|180|270` make the display transform explicit without
  changing the renderer.
- Prints `TOUCH_DOWN`, `TOUCH_MOVE`, and `TOUCH_UP` lines to stdout, handles
  `SIGINT`/`SIGTERM` through a flag, polls with a finite timeout, and closes the
  selected descriptor on exit. It does not use `EVIOCGRAB` or write input
  device state.

Local validation passed with `make TARGET=host build/inkboy-host
build/touch_probe-host`, `bash -n bin/deploy`, and `git diff --check`. Running
the host probe found no `/dev/input` directory, so it produced no touchscreen
evidence locally. The Kindle build and deployment were subsequently run on the
target, where the probe identified the touchscreen and produced touch events.

## Initial physical PW5 evidence

During an initial run on the target Kindle, the user reported real movement and
release output, including:

```text
TOUCH_MOVE x=710 y=1110
TOUCH_MOVE x=685 y=1096
TOUCH_MOVE x=658 y=1081
TOUCH_MOVE x=630 y=1066
TOUCH_MOVE x=601 y=1051
TOUCH_UP x=477 y=984
```

This confirms that the deployed probe can read touch packets and emit
normalized screen coordinates on the physical PW5. The excerpt does not record
the selected event path, device name, reported axis ranges, corner mapping,
orientation, or whether the process was stopped cleanly, so it is not yet a
complete touchscreen compatibility record.

A subsequent run recorded the device metadata and a press event:

```text
DEVICE path=/dev/input/event1 name="pt_mt" protocol=type-b direct=yes axes=x[0,1235] y[0,1647]
SELECTED path=/dev/input/event1 name="pt_mt" display=1236x1648 orientation=0 protocol=type-b
TOUCH_DOWN x=957 y=885
```

This is the first direct touchscreen evidence from the physical PW5 running
firmware 5.19.2. The verified device facts are:

- event path: `/dev/input/event1` on this test device;
- device name: `pt_mt`;
- protocol: type-B multi-touch;
- property: direct touchscreen;
- X range: `0..1235`;
- Y range: `0..1647`;
- reported slot count: 32, with the probe intentionally limiting its local
  state table to 16 slots; and
- current tested display transform: 1236×1648, orientation `0`.

The matching axis ranges strongly support the default unrotated mapping. The
user subsequently confirmed the four-corner test, with no inversion, axis swap,
or offset observed.

The user also confirmed that press, movement, and release handling worked
while the Kindle interface was open, that the probe did not interfere with the
normal interface, and that stopping the process completed cleanly. The
existing FBInk rendering and restoration behavior remained unchanged.

The hardware validation procedure used was:

1. On the PW5, list `/dev/input/event*`, `/sys/class/input/event*/device/name`,
   and permissions without changing any device state.
2. In C, enumerate candidate nodes, query `EVIOCGNAME`, `EVIOCGBIT`,
   `EVIOCGABS`, and input properties, and print a capability summary.
3. Select a candidate by verified capabilities, open it read-only and
   non-blocking, and poll it with a finite timeout.
4. Print every relevant raw `EV_ABS`, `EV_KEY`, and `EV_SYN` record, including
   slot, tracking ID, raw X/Y, and packet boundaries.
5. Touch the four corners and the center, then record the raw ranges,
   orientation, press/move/release sequence, and any `SYN_DROPPED` events.
6. Stop with Ctrl-C or `SIGTERM`; verify that the fd closes, the process exits,
   no Kindle UI behavior was changed, and the existing FBInk renderer still
   saves, refreshes, and restores exactly as before.

The probe now provides verified target evidence. The following milestone can
add a touch hit test for a rendered `START` button; it should remain a separate
change from this probe implementation.

## Hardware verification checklist

Touchscreen compatibility is verified for the tested PW5 running firmware
5.19.2. The completed checks are:

- the selected `/dev/input/eventX` path and stable device name;
- the permissions available to the deployed InkBoy process;
- supported event types, MT/single-touch codes, and properties;
- X/Y minimum and maximum values from the device, not guessed screen pixels;
- corner mapping, axis inversion/swapping, rotation, and any offset;
- press, movement, release, and multi-touch behavior;
- behavior while the Kindle's original interface is running;
- clean exit and descriptor closure after Ctrl-C and `SIGTERM`; and
- unchanged Stage 2 rendering and normal framebuffer restoration.

The correct project statement is that InkBoy has a standalone evdev probe with
verified touchscreen input on the tested PW5. This does not imply that event
numbering or calibration is identical on other Kindle models or firmware
versions.
