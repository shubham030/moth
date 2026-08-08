// The one import a program needs:
//
//   import 'package:moth/moth.dart';
//
// moth has no per-library namespaces — every unit the compiler loads shares
// one global scope — so importing this file brings in everything it imports.
// That is why this is a list of imports rather than exports, and why a program
// that wants only pins can import 'package:moth/hardware.dart' directly and
// leave the widget layer out of the blob.

import 'hardware.dart';
import 'widgets.dart';
