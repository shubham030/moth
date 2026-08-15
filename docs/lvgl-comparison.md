---
title: moth vs LVGL
sidebar_position: 7
slug: /lvgl-comparison
---

# The same app, twice

One pomodoro timer — 25 minutes of focus, a draining ring, tap to start —
written both ways. Both versions below are real: the Dart one is
[`examples/ui/pomodoro.dart`](https://github.com/shubham030/moth/blob/main/examples/ui/pomodoro.dart)
and runs on the ESP32-S3 panel; the C one is
[`examples/comparison/pomodoro_lvgl.c`](https://github.com/shubham030/moth/blob/main/examples/comparison/pomodoro_lvgl.c),
compiles against LVGL release/v9.2 with zero warnings, and runs in LVGL's
SDL simulator. Neither is a strawman — the LVGL version is written the way
LVGL wants to be written.

moth on the left, LVGL on the right, both screenshotted from their
simulators:

![the same pomodoro in moth and LVGL](img/pomodoro-side-by-side.png)

(The one visible difference is the clock: moth's is 72px from a subsetted
face; LVGL's stock fonts stop at Montserrat 48, and going bigger means the
offline font converter.)

## The moth version

```dart
import 'package:moth/widgets.dart';

final workMs = 25 * 60 * 1000;
final breakMs = 5 * 60 * 1000;

final workAccent = 0xFFE8A33D; // amber: time to focus
final breakAccent = 0xFF33AA66; // green: time to stand up

class Pomodoro extends Component {
  bool running = false;
  bool onBreak = false;
  int remainingMs = 25 * 60 * 1000;
  int endMs = 0; // wall-clock end of the running session
  int shownSec = -1; // last second painted

  void toggle() {
    setState(() {
      running = !running;
      if (running) endMs = millis() + remainingMs;
    });
  }

  void tick(int now) {
    if (!running) return;
    remainingMs = endMs - now;
    if (remainingMs <= 0) {
      setState(() {
        onBreak = !onBreak;
        remainingMs = onBreak ? breakMs : workMs;
        running = false;
      });
      return;
    }
    if (remainingMs ~/ 1000 != shownSec) {
      setState(() {
        shownSec = remainingMs ~/ 1000;
      });
    }
  }

  Widget build() {
    var accent = onBreak ? breakAccent : workAccent;
    var total = onBreak ? breakMs : workMs;
    var m = remainingMs ~/ 60000;
    var s = (remainingMs ~/ 1000) % 60;

    return Container(
      color: 0xFF0E0E12,
      onTap: () => toggle(),
      child: Stack(children: [
        Container(
          color: 0xFF0E0E12,
          flex: 1,
          padding: uiSafeArea(0),
          child: Column(
            mainAxisAlignment: mainAxisCenter,
            crossAxisAlignment: crossAxisCenter,
            spacing: 14,
            children: [
              Text(onBreak ? 'BREAK' : 'FOCUS',
                  style: TextStyle(fontSize: 20, color: accent)),
              Text('$m:${s < 10 ? "0$s" : "$s"}',
                  style: TextStyle(fontSize: 72, color: 0xFFF2EFE7)),
              Divider(thickness: 2, width: 180, color: 0xFF2A2A31),
              Text(running ? 'TAP TO PAUSE' : 'TAP TO START',
                  style: TextStyle(fontSize: 14, color: 0xFF6B6B76)),
            ],
          ),
        ),
        CircularProgressIndicator(
          value: remainingMs / total,
          strokeWidth: 8,
          color: accent,
          backgroundColor: 0xFF1C1C21,
          size: uiWidth(),
        ),
      ]),
    );
  }
}

void main() {
  var pomo = Pomodoro();
  runApp(pomo);

  var last = millis();
  while (true) {
    var now = millis();
    pomo.tick(now);
    pumpFrame(now - last);
    last = now;
    delay(50);
  }
}
```

## The LVGL version

The full file is
[`examples/comparison/pomodoro_lvgl.c`](https://github.com/shubham030/moth/blob/main/examples/comparison/pomodoro_lvgl.c);
this is its shape:

```c
/* State lives in globals; the widgets that display it live in globals too,
 * because callbacks need to reach both. */
static bool running;
static bool on_break;
static int32_t remaining_ms = WORK_MS;
static uint32_t end_ms;

static lv_obj_t *arc;
static lv_obj_t *phase_label;
static lv_obj_t *time_label;
static lv_obj_t *hint_label;

/* Every piece of state that changes must be manually pushed into each
 * widget that shows it — this is the retained-mode contract. Forget one
 * call here and that widget silently shows stale state. */
static void refresh(void) {
  int32_t total = on_break ? BREAK_MS : WORK_MS;
  int32_t sec = remaining_ms / 1000;

  lv_label_set_text(phase_label, on_break ? "BREAK" : "FOCUS");
  lv_obj_set_style_text_color(phase_label, accent(), 0);
  lv_label_set_text_fmt(time_label, "%ld:%02ld", (long)(sec / 60),
                        (long)(sec % 60));
  lv_label_set_text(hint_label, running ? "TAP TO PAUSE" : "TAP TO START");
  lv_arc_set_value(arc, (int32_t)(((int64_t)remaining_ms * 100) / total));
  lv_obj_set_style_arc_color(arc, accent(), LV_PART_INDICATOR);
}

void pomodoro_create(void) {
  /* ...54 lines of lv_obj_create / lv_obj_set_style_* / lv_obj_set_flex_*
   * building the tree that build() describes declaratively — including
   * eight calls to turn lv_arc (an input widget) into a progress ring:
   * rotate it, set its range, strip its knob, make it non-clickable. */
}
```

## What actually differs

| | moth | LVGL |
| --- | --- | --- |
| Model | declarative — `build()` *is* the state; the reconciler diffs it | retained — every change hand-routed to every widget showing it |
| The classic bug | "forgot to update the label" is not expressible | a missed call in `refresh()` = silently stale widget |
| State topology | fields on one class, reached via `this` | 8 globals, because callbacks must reach state *and* widgets |
| The ring | `CircularProgressIndicator(value:)` | `lv_arc` styled out of being a slider, in eight calls |
| Fonts | `fontSize: 72`, faces subsetted offline by `fontgen` | `lv_conf.h` edit + rebuild; stock ceiling is 48px |
| Round panel | `uiSafeArea()` | inscribed square computed by hand (329px) |
| Edit-to-screen | `mothc --push`: **175ms** over the cable, program only | full firmware flash — the UI is compiled into it |
| Line count | 113 with comments | 123, excluding bring-up and `lv_conf.h` |

The pitch is not "less code" — the counts are nearly equal. It is *which*
code: describing the UI versus plumbing state into it, and a 175ms push
versus a firmware flash for every iteration.

## What LVGL has that moth does not

Being honest cuts both ways. LVGL is a decade mature with a widget
catalogue moth will not match for years (tables, charts, keyboards, spans,
animations galore), theming, RTL and CJK text, binding ecosystems, and a
huge community. moth trades all of that breadth for one bet: a beginner who
knows Dart — or wants to learn the language Flutter apps are written in —
can be productive on a microcontroller in an afternoon, with the
programming model they will meet again on mobile. If you need LVGL's
breadth today, use LVGL; it is excellent.
