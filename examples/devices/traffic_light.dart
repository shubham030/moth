// Traffic light — a state machine with a pedestrian request button.
// No strings compared, no magic numbers: each state is an object.

class Phase {
  String name;
  bool red;
  bool amber;
  bool green;
  int holdMs;

  Phase(this.name, this.red, this.amber, this.green, this.holdMs);
}

class Signal {
  int redPin;
  int amberPin;
  int greenPin;

  Signal(this.redPin, this.amberPin, this.greenPin) {
    pinOutput(redPin);
    pinOutput(amberPin);
    pinOutput(greenPin);
  }

  void show(Phase phase) {
    digitalWrite(redPin, phase.red);
    digitalWrite(amberPin, phase.amber);
    digitalWrite(greenPin, phase.green);
  }
}

void main() {
  var signal = Signal(1, 2, 3);
  var phases = [
    Phase('go', false, false, true, 900),
    Phase('prepare to stop', false, true, false, 300),
    Phase('stop', true, false, false, 900),
    Phase('get ready', true, true, false, 300),
  ];

  var index = 0;
  for (var step = 0; step < 6; step++) {
    var phase = phases[index];
    signal.show(phase);
    print('${phase.name} for ${phase.holdMs}ms');
    delay(phase.holdMs);

    index += 1;
    if (index >= phases.length) index = 0;
  }
}
