/// A POSIX serial port, opened the way a serial port has to be opened.
///
/// Dart's File API cannot do this job: opening a modem-class device without
/// O_NONBLOCK can block indefinitely on carrier — /dev/cu.usbmodem* does
/// exactly that on macOS when the CDC line state is unlucky, and whether it
/// is unlucky depends on what the previous program left DTR at. Shelling out
/// to stty has the same flaw (its own open blocks the same way) plus a PATH
/// dependency. So: open(2) with O_NONBLOCK|O_NOCTTY via FFI, CLOCAL so modem
/// control lines never matter again, and termios raw mode so the tty layer
/// neither cooks the frame's bytes on the way out (ONLCR would corrupt every
/// 0x0A in the blob) nor line-buffers the ack on the way in.
///
/// macOS and Linux only. The struct offsets and flag values are the two
/// ABIs' actual numbers, kept side by side and selected at run time.
library;

import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

typedef _OpenC = Int32 Function(Pointer<Uint8> path, Int32 flags);
typedef _OpenD = int Function(Pointer<Uint8> path, int flags);
typedef _RwC = IntPtr Function(Int32 fd, Pointer<Uint8> buf, IntPtr n);
typedef _RwD = int Function(int fd, Pointer<Uint8> buf, int n);
typedef _CloseC = Int32 Function(Int32 fd);
typedef _CloseD = int Function(int fd);
typedef _TcC = Int32 Function(Int32 fd, Pointer<Uint8> t);
typedef _TcD = int Function(int fd, Pointer<Uint8> t);
typedef _TcSetC = Int32 Function(Int32 fd, Int32 act, Pointer<Uint8> t);
typedef _TcSetD = int Function(int fd, int act, Pointer<Uint8> t);
typedef _ErrnoC = Pointer<Int32> Function();
typedef _ErrnoD = Pointer<Int32> Function();

final _libc = DynamicLibrary.process();
final _open = _libc.lookupFunction<_OpenC, _OpenD>('open');
final _read = _libc.lookupFunction<_RwC, _RwD>('read');
final _write = _libc.lookupFunction<_RwC, _RwD>('write');
final _close = _libc.lookupFunction<_CloseC, _CloseD>('close');
final _tcgetattr = _libc.lookupFunction<_TcC, _TcD>('tcgetattr');
final _tcsetattr = _libc.lookupFunction<_TcSetC, _TcSetD>('tcsetattr');
final _errnoLoc = _libc.lookupFunction<_ErrnoC, _ErrnoD>(
    Platform.isMacOS ? '__error' : '__errno_location');

int get _errno => _errnoLoc().value;

// open(2) flags. O_RDWR is 2 everywhere; the rest differ.
final int _oNonblock = Platform.isMacOS ? 0x0004 : 0x800;
final int _oNoctty = Platform.isMacOS ? 0x20000 : 0x100;

// c_cflag bits: keep receiving, ignore modem control lines.
final int _cread = Platform.isMacOS ? 0x800 : 0x80;
final int _clocal = Platform.isMacOS ? 0x8000 : 0x800;

const _eagain = 35; // macOS; Linux EAGAIN is 11
const _eagainLinux = 11;

bool _wouldBlock(int err) => err == (Platform.isMacOS ? _eagain : _eagainLinux);

class SerialException implements Exception {
  final String message;
  SerialException(this.message);
  @override
  String toString() => message;
}

class SerialPort {
  final int _fd;
  final Pointer<Uint8> _buf;
  static const _bufSize = 4096;

  SerialPort._(this._fd, this._buf);

  /// Opens [path] non-blocking and puts it in raw mode. Throws
  /// [SerialException] with the reason otherwise.
  factory SerialPort.open(String path) {
    final bytes = utf8.encode(path);
    final cPath = _alloc(bytes.length + 1);
    cPath.asTypedList(bytes.length + 1)
      ..setAll(0, bytes)
      ..[bytes.length] = 0;
    final fd = _open(cPath, 2 /* O_RDWR */ | _oNonblock | _oNoctty);
    _free(cPath);
    if (fd < 0) {
      throw SerialException('cannot open $path (errno $_errno) — '
          'is a serial monitor holding it?');
    }
    _makeRaw(fd, path);
    return SerialPort._(fd, _alloc(_bufSize));
  }

  /// Raw mode via termios, treated as an opaque buffer with the two ABIs'
  /// field offsets. Zeroing iflag/oflag/lflag is cfmakeraw and then some —
  /// no flow control, no echo, no line discipline — which is exactly right
  /// for a byte pipe to a microcontroller.
  static void _makeRaw(int fd, String path) {
    final t = _alloc(128); // both ABIs' struct termios fit comfortably
    if (_tcgetattr(fd, t) != 0) {
      _free(t);
      _close(fd);
      throw SerialException('$path is not a terminal (tcgetattr failed)');
    }
    if (Platform.isMacOS) {
      // 64-bit fields: iflag@0 oflag@8 cflag@16 lflag@24.
      t.cast<Uint64>()[0] = 0;
      t.cast<Uint64>()[1] = 0;
      t.cast<Uint64>()[2] = (t.cast<Uint64>()[2] | _cread | _clocal);
      t.cast<Uint64>()[3] = 0;
    } else {
      // 32-bit fields: iflag@0 oflag@4 cflag@8 lflag@12.
      t.cast<Uint32>()[0] = 0;
      t.cast<Uint32>()[1] = 0;
      t.cast<Uint32>()[2] = (t.cast<Uint32>()[2] | _cread | _clocal);
      t.cast<Uint32>()[3] = 0;
    }
    final rc = _tcsetattr(fd, 0 /* TCSANOW */, t);
    _free(t);
    if (rc != 0) {
      _close(fd);
      throw SerialException('cannot configure $path (tcsetattr failed)');
    }
  }

  /// Whatever is waiting, up to 4KB; empty when nothing is. Never blocks.
  Uint8List readAvailable() {
    final n = _read(_fd, _buf, _bufSize);
    if (n > 0) return Uint8List.fromList(_buf.asTypedList(n));
    if (n < 0 && !_wouldBlock(_errno)) {
      throw SerialException('read failed (errno $_errno) — cable unplugged?');
    }
    return Uint8List(0);
  }

  /// Writes all of [data], riding out partial writes and EAGAIN — a 4KB USB
  /// CDC buffer fills fast at frame sizes.
  void writeAll(Uint8List data) {
    var off = 0;
    while (off < data.length) {
      final chunk = data.length - off > _bufSize ? _bufSize : data.length - off;
      _buf.asTypedList(chunk).setAll(0, data.sublist(off, off + chunk));
      final n = _write(_fd, _buf, chunk);
      if (n > 0) {
        off += n;
      } else if (n < 0 && !_wouldBlock(_errno)) {
        throw SerialException('write failed (errno $_errno) — '
            'cable unplugged?');
      } else {
        sleep(const Duration(milliseconds: 5));
      }
    }
  }

  void close() {
    _close(_fd);
    _free(_buf);
  }
}

// malloc/free straight from libc, so no package:ffi dependency is needed.
typedef _MallocC = Pointer<Uint8> Function(IntPtr size);
typedef _MallocD = Pointer<Uint8> Function(int size);
typedef _FreeC = Void Function(Pointer<Uint8> p);
typedef _FreeD = void Function(Pointer<Uint8> p);
final _alloc = _libc.lookupFunction<_MallocC, _MallocD>('malloc');
final _free = _libc.lookupFunction<_FreeC, _FreeD>('free');
