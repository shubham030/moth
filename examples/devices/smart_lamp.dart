// Smart lamp — a dimmable light with preset levels and a button that cycles
// through them. Shows PWM output wrapped in an object.

class PwmOutput {
  int pin;
  int duty = 0;

  PwmOutput(this.pin);

  void set(int value) {
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    duty = value;
    analogWrite(pin, duty);
  }

  int percent() {
    return duty * 100 ~/ 255;
  }
}

class Lamp {
  PwmOutput output;
  List<int> presets;
  int level = 0;

  Lamp(this.output, this.presets);

  void next() {
    level += 1;
    if (level >= presets.length) level = 0;
    output.set(presets[level]);
  }

  String status() {
    return 'level $level — ${output.percent()}% brightness';
  }
}

void main() {
  var lamp = Lamp(PwmOutput(6), [0, 64, 160, 255]);

  for (var press = 0; press < 5; press++) {
    lamp.next();
    print(lamp.status());
    delay(400);
  }
}
