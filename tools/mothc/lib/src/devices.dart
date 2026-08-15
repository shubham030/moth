/// Where a moth program can run: boards on serial ports, and the desktop
/// simulator when its binary is built. `mothc devices` lists them;
/// `mothc run` picks one the way flutter run does — explicitly with -d,
/// automatically when the choice is unambiguous.
library;

import 'dart:io';

import 'package:path/path.dart' as p;

enum DeviceKind { board, simulator }

class Device {
  final String id; // what -d matches: a serial path, or "sim"
  final DeviceKind kind;
  final String description;

  Device(this.id, this.kind, this.description);

  @override
  String toString() => '$id  ·  $description';
}

/// Serial ports that could be a board. Every USB-serial candidate is
/// listed — nothing here probes them, because the probe itself (opening
/// the port) resets most dev boards, and "listing devices rebooted my
/// board" is a terrible surprise.
List<Device> discoverBoards() {
  final devices = <Device>[];
  final dev = Directory('/dev');
  if (!dev.existsSync()) return devices;
  for (final e in dev.listSync(followLinks: false)) {
    final name = p.basename(e.path);
    if (name.startsWith('cu.usbmodem') || // macOS
        name.startsWith('ttyUSB') || // Linux, USB-serial bridge
        name.startsWith('ttyACM')) {
      // Linux, CDC
      devices.add(Device(e.path, DeviceKind.board, 'serial board'));
    }
  }
  devices.sort((a, b) => a.id.compareTo(b.id));
  return devices;
}

/// The mothsim binary in a moth checkout, or null. Same anchors the
/// compiler resolves package: imports against, so `run` and the rest of
/// the toolchain agree on which checkout is "here".
String? findMothsim() {
  final fromScript =
      p.dirname(p.dirname(p.absolute(Platform.script.toFilePath())));
  final cwd = Directory.current.path;
  for (final guess in [
    p.join(fromScript, '..', '..', 'build', 'mothsim'),
    p.join(fromScript, '..', 'build', 'mothsim'),
    p.join(cwd, 'build', 'mothsim'),
    p.join(cwd, '..', 'build', 'mothsim'),
    p.join(cwd, '..', '..', 'build', 'mothsim'),
  ]) {
    final path = p.normalize(guess);
    if (File(path).existsSync()) return path;
  }
  return null;
}

List<Device> discoverDevices() {
  final devices = discoverBoards();
  final sim = findMothsim();
  if (sim != null) {
    devices.add(Device('sim', DeviceKind.simulator, 'desktop simulator ($sim)'));
  }
  return devices;
}

/// Picks the device to run on, or null when the choice needs the user.
///
/// -d matches an id exactly or as an unambiguous substring, so
/// `-d usbmodem2101` works without the /dev/cu. spelling. With no flag:
/// exactly one board wins (plugging in a board is the statement of
/// intent); no board falls back to the simulator when it is built.
Device? selectDevice(List<Device> devices, String? flag) {
  if (flag != null) {
    final exact = devices.where((d) => d.id == flag).toList();
    if (exact.length == 1) return exact.first;
    final partial = devices.where((d) => d.id.contains(flag)).toList();
    if (partial.length == 1) return partial.first;
    return null;
  }
  final boards = devices.where((d) => d.kind == DeviceKind.board).toList();
  if (boards.length == 1) return boards.first;
  if (boards.isEmpty) {
    final sims = devices.where((d) => d.kind == DeviceKind.simulator);
    if (sims.length == 1) return sims.first;
  }
  return null;
}
