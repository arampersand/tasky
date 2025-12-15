#pragma once

// Fix conflict with Arduino/ESP32 macros
// We undefine them locally but restore them?
// Actually, if we just include the library and it uses "Ecc::LOW", it might
// conflict if LOW is defined as 0. But we cannot undefine it globally. We will
// rely on the fact that C++ scoped enums usually work unless the macro is a
// function-like macro or simple replacement. LOW is simple replacement '0x0'.
// "class Ecc { LOW = 0 }" -> "class Ecc { 0x0 = 0 }" -> ERROR.
// So we MUST undefine LOW/HIGH before including qrcodegen.hpp.
// BUT we must restore them for subsequent code!

#pragma push_macro("LOW")
#pragma push_macro("HIGH")
#undef LOW
#undef HIGH

#include "qrcodegen.hpp"

#pragma pop_macro("LOW")
#pragma pop_macro("HIGH")

// Helper structs for compatibility with previous C-style usage in the project
struct QRCode {
  uint8_t version;
  uint8_t size;
  uint8_t *data;
  // We need to store the QrCode object or data somehow if we want to be fully
  // robust, but here we are mimicking the C-struct style the user code expects.
};

// Map ECC to Nayuki enum
// Note: The Nayuki library uses qrcodegen::QrCode::Ecc::LOW etc.
// The previous qrcode.h had C-style mapping. We need to adapt it to the C++
// library.

using namespace qrcodegen;

// Safe definitions avoiding the tokens LOW/HIGH which are macros in Arduino
#define ECC_LOW (qrcodegen::QrCode::Ecc)0
#define ECC_MEDIUM (qrcodegen::QrCode::Ecc)1
#define ECC_QUARTILE (qrcodegen::QrCode::Ecc)2
#define ECC_HIGH (qrcodegen::QrCode::Ecc)3

// Nayuki's C++ library doesn't expose raw buffer size macros easily in the
// header usually. We might need to implement the helper logic using the C++
// API. "qrcodegen_BUFFER_LEN_FOR_VERSION" looked like C API. The file we saw in
// qrcodegen.c was definitely C++ source code (namespace, vector, etc). So we
// must use the C++ API.

static inline int qrcode_getBufferSize(uint8_t version) {
  // getNumRawDataModules is private.
  // We can just estimate buffer size.
  // Max version 40 is ~3KB. Version 3 is tiny.
  // Let's just return a safe upper bound formula: (16*ver + 128)*ver + 64 bits
  // -> /8 bytes. Or just a fixed safe buffer.
  return ((16 * version + 128) * version + 64) / 8 + 20;
}

// We need to adapt the qrcode_initText to use the C++ class QrCode from
// qrcodegen The User's code expects: qrcode_initText(&qrcode, qrcodeData,
// version, ecc, payload.c_str()); where 'qrcode' is the struct, qrcodeData is a
// raw buffer. Nayuki C++ API returns a QrCode object which holds vectors. We
// can't easily map the C++ vector-based object to a raw buffer pointer expected
// by `drawQRFull` unless we render it to that buffer.

// CRITICAL: The user has `qrcodegen.c` which turned out to be C++.
// But the user *also* had `qrcode.h` which looked like a C wrapper.
// The error log showed `qrcodegen.c` content was effectively `QrCode generator
// library (C++)`. So we must use the C++ class natively.

// Let's change the `struct QRCode` to actually hold the C++ object or
// equivalent data processing. But `ui.cpp` declares `uint8_t qrcodeData[...]`
// on stack. Nayuki C++ uses dynamic memory (std::vector). We should probably
// rewrite `drawQRFull` in `ui.cpp` to use the C++ API directly instead of this
// wrapper, OR make this wrapper do the translation.

// Since we can't easily rewrite ui.cpp yet (lots of changes), let's try to make
// this header adaptable. But wait, `qrcode_initText` in `ui.cpp` passes a
// buffer. Nayuki C++ `encodeText` returns a `QrCode` object on stack/heap
// (return by value). We should change `ui.cpp` to just use `qrcodegen::QrCode`
// directly? Yes, that's cleaner.

// But for now, to fix the specific errors, I will remove the extern C and adapt
// the wrapper functions to basic C++.

static inline bool qrcode_getModule(const QRCode *q, int x,
                                    int y); // Forward decl

// We will implement a temporary hack to make `ui.cpp` compile without rewriting
// it completely yet, or we rewrite `ui.cpp` part too. Rewriting `ui.cpp` is
// safer.

// Actually, let's keep the struct but we will ignore the `data` pointer in some
// ways or fill it? Nayuki provides accessors `getModule(x,y)`. We can't easily
// dump the internal format to a flat buffer that matches what
// `qrcode_getModule` expected if it was a different library. The previous
// library I saw in `ui.cpp` seemed to be `ricmoo/QRCode` (C library). The user
// provided `qrcodegen` (Nayuki) which is a DIFFERENT library. The APIs are
// incompatible. `ricmoo` uses `qrcode_initText`, `qrcode_getModule`. `Nayuki`
// uses `QrCode::encodeText`, `qr.getModule`.

// I MUST convert `ui.cpp` to use Nayuki's API because bridging the C buffer API
// to Nayuki's vector API is painful/inefficient. So I will strip this
// `qrcode.h` to just include the Nayuki header and provide necessary usings,
// and then I will update `ui.cpp` to use the C++ API.
