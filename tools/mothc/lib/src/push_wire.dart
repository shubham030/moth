/// The push wire format, sender's side — vm/host/push_proto.h is the
/// format's home and this file must agree with it byte for byte.
///
/// A frame is "MPSH", u32 little-endian length, u32 nonce, then the blob.
/// The receiver's reply is 8 bytes — "MPOK" or "MPRJ" plus the echoed nonce —
/// sent after verification. The nonce is what makes the reply unforgeable:
/// no log line, no program's own print(), and no stale reply from an earlier
/// push can contain a random number invented after all of them.
library;

import 'dart:convert';
import 'dart:typed_data';

import 'package:crypto/crypto.dart';

Uint8List pushFrame(Uint8List blob, int nonce) {
  final b = BytesBuilder()
    ..add(ascii.encode('MPSH'))
    ..add([
      blob.length & 0xFF,
      (blob.length >> 8) & 0xFF,
      (blob.length >> 16) & 0xFF,
      (blob.length >> 24) & 0xFF,
      nonce & 0xFF,
      (nonce >> 8) & 0xFF,
      (nonce >> 16) & 0xFF,
      (nonce >> 24) & 0xFF,
    ])
    ..add(blob);
  return b.toBytes();
}

/// The key a pairing phrase derives: its SHA-256, the same 32 bytes
/// provision.py stores on the board. Deriving on both ends means the phrase
/// itself never exists anywhere but the two keyboards it was typed on.
Uint8List keyFromPassphrase(String phrase) =>
    Uint8List.fromList(sha256.convert(utf8.encode(phrase)).bytes);

/// The authenticated frame: "MPH2", length, nonce, then HMAC-SHA256 with
/// [key] over the nonce's wire bytes followed by the blob, then the blob.
/// The receiver recomputes the same MAC before the blob reaches its
/// verifier; vm/host/push_proto.h is the format's home.
Uint8List pushFrameAuthed(Uint8List blob, int nonce, List<int> key) {
  final nonceLe = [
    nonce & 0xFF,
    (nonce >> 8) & 0xFF,
    (nonce >> 16) & 0xFF,
    (nonce >> 24) & 0xFF,
  ];
  final mac = Hmac(sha256, key).convert([...nonceLe, ...blob]);
  final b = BytesBuilder()
    ..add(ascii.encode('MPH2'))
    ..add([
      blob.length & 0xFF,
      (blob.length >> 8) & 0xFF,
      (blob.length >> 16) & 0xFF,
      (blob.length >> 24) & 0xFF,
    ])
    ..add(nonceLe)
    ..add(mac.bytes)
    ..add(blob);
  return b.toBytes();
}

Uint8List verdictReply(String cc, int nonce) => Uint8List.fromList([
      ...ascii.encode(cc),
      nonce & 0xFF,
      (nonce >> 8) & 0xFF,
      (nonce >> 16) & 0xFF,
      (nonce >> 24) & 0xFF,
    ]);

/// Watches a byte stream for the verdict answering [nonce].
///
/// Feed each received chunk; the scan covers only the new bytes plus a
/// 7-byte carry from the previous call, so cost is O(chunk) no matter how
/// much console output the stream accumulates — the first version rescanned
/// the whole session's log from index zero on every 20ms poll.
class VerdictScanner {
  final Uint8List _ok;
  final Uint8List _rj;
  final List<int> _window = [];

  VerdictScanner(int nonce)
      : _ok = verdictReply('MPOK', nonce),
        _rj = verdictReply('MPRJ', nonce);

  /// True = accepted, false = rejected, null = no verdict yet.
  bool? feed(List<int> chunk) {
    _window.addAll(chunk);
    final hit = _find(_ok) ? true : (_find(_rj) ? false : null);
    // A reply spans at most 8 bytes; 7 carried bytes cover every split.
    if (_window.length > 7) {
      _window.removeRange(0, _window.length - 7);
    }
    return hit;
  }

  bool _find(Uint8List needle) {
    outer:
    for (var i = 0; i + needle.length <= _window.length; i++) {
      for (var j = 0; j < needle.length; j++) {
        if (_window[i + j] != needle[j]) continue outer;
      }
      return true;
    }
    return false;
  }
}
