// A pomodoro timer for a round panel: 25 minutes of focus, 5 of break, a
// ring that drains as the session runs, and a tap anywhere to start or
// pause. Everything on screen is driven by setState — the loop at the
// bottom only feeds wall-clock time in.

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
  int shownSec = -1; // last second painted, so ticks repaint once a second

  void toggle() {
    setState(() {
      running = !running;
      if (running) endMs = millis() + remainingMs;
    });
  }

  /// Called every frame with the current clock. Cheap when nothing changed:
  /// setState only runs when the visible second rolls over.
  void tick(int now) {
    if (!running) return;
    remainingMs = endMs - now;
    if (remainingMs <= 0) {
      // Session over: swap focus/break and wait for a tap to start it.
      setState(() {
        onBreak = !onBreak;
        remainingMs = onBreak ? breakMs : workMs;
        running = false;
      });
      return;
    }
    // Closures reach fields, not locals — so the lambda recomputes from the
    // field rather than capturing a temporary.
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
      child: Stack(
        children: [
          Container(
            color: 0xFF0E0E12,
            flex: 1,
            padding: uiSafeArea(0),
            child: Column(
              mainAxisAlignment: mainAxisCenter,
              crossAxisAlignment: crossAxisCenter,
              spacing: 14,
              children: [
                Text(
                  onBreak ? 'BREAK' : 'FOCUS',
                  style: TextStyle(fontSize: 20, color: accent),
                ),
                Text(
                  '$m:${s < 10 ? "0$s" : "$s"}',
                  style: TextStyle(fontSize: 72, color: 0xFFF2EFE7),
                ),
                Divider(thickness: 2, width: 180, color: 0xFF2A2A31),
                Text(
                  running ? 'TAP TO PAUSE' : 'TAP TO START',
                  style: TextStyle(fontSize: 14, color: 0xFF6B6B76),
                ),
              ],
            ),
          ),
          // The ring drains with the session: full at the start, empty at 0.
          CircularProgressIndicator(
            value: remainingMs / total,
            strokeWidth: 8,
            color: accent,
            backgroundColor: 0xFF1C1C21,
            size: uiWidth(),
          ),
        ],
      ),
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
