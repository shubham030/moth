// Image('path.png') embeds the decoded file in the blob's assets section.
// These tests pin the compiler half: the file is found relative to the
// program, decoded pixels land in the blob byte-for-byte, a computed path
// and a missing file fail with human messages, and the size budget holds.
// The loader half (parsing, alignment, borrowing) is pinned by the C tests
// and the simulator run.
import 'dart:io';
import 'dart:typed_data';

import 'package:image/image.dart' as img;
import 'package:mothc/src/compiler.dart';
import 'package:mothc/src/errors.dart';
import 'package:test/test.dart';

// A class named Image is enough to make the constructor call compile; the
// embed hook keys on the call's shape, not on package:moth.
const stub = "class Image { var src; Image(this.src); }\n";

Uint8List compileIn(Directory dir, String program) {
  final main = File('${dir.path}/main.dart')..writeAsStringSync(program);
  return Compiler(main.path, program).compile().blob;
}

/// True when [needle] occurs in [hay] — the blob has no framing ambiguity
/// for a long distinctive pixel run.
bool bytesContain(Uint8List hay, List<int> needle) {
  outer:
  for (var i = 0; i + needle.length <= hay.length; i++) {
    for (var j = 0; j < needle.length; j++) {
      if (hay[i + j] != needle[j]) continue outer;
    }
    return true;
  }
  return false;
}

void main() {
  late Directory tmp;
  setUp(() => tmp = Directory.systemTemp.createTempSync('moth-img'));
  tearDown(() => tmp.deleteSync(recursive: true));

  test('a PNG round-trips into the blob, pixels exact', () {
    // 3x2 with distinctive channel values, including alpha.
    final im = img.Image(width: 3, height: 2, numChannels: 4);
    var v = 10;
    for (final p in im) {
      p
        ..r = v
        ..g = v + 1
        ..b = v + 2
        ..a = 200 + (v ~/ 10);
      v += 10;
    }
    File('${tmp.path}/pix.png').writeAsBytesSync(img.encodePng(im));

    final blob = compileIn(
        tmp, "${stub}void main() { var i = Image('pix.png'); print(i.src); }");

    // The wire order is B,G,R,A per pixel; the first two pixels make an
    // 8-byte needle that cannot occur by chance.
    expect(bytesContain(blob, [12, 11, 10, 201, 22, 21, 20, 202]), isTrue,
        reason: 'decoded pixels should appear in the blob verbatim');
    // And the key travels as a constant string.
    expect(bytesContain(blob, 'pix.png'.codeUnits), isTrue);
  });

  test('a missing file is a compile error naming the file', () {
    expect(
        () => compileIn(
            tmp, "${stub}void main() { print(Image('nope.png').src); }"),
        throwsA(isA<CompileError>()
            .having((e) => e.message, 'message', contains('nope.png'))));
  });

  test('a computed path is refused — nothing to embed', () {
    expect(
        () => compileIn(tmp, '''
$stub
void main() {
  var name = 'logo.png';
  print(Image(name).src);
}
'''),
        throwsA(isA<CompileError>()
            .having((e) => e.message, 'message', contains('literal path'))));
  });

  test('the size budget fails with the total, not a mystery', () {
    final big = img.Image(width: 400, height: 400, numChannels: 4);
    File('${tmp.path}/big.png').writeAsBytesSync(img.encodePng(big));
    expect(
        () => compileIn(
            tmp, "${stub}void main() { print(Image('big.png').src); }"),
        throwsA(isA<CompileError>()
            .having((e) => e.message, 'message', contains('budget'))));
  });
}
