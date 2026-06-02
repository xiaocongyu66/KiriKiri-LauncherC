qrcode.dll compatibility source
================================

`QR_Encode.cpp` and `QR_Encode.h` were imported from the KiriKiri qrcode
plugin collection under `/root/krkr-w/游戏/test/qrcode`.

The plugin readme states that the plugin license follows the KiriKiri engine
license, and credits Psytec Inc. for the QR encoder source.

Local changes:

- Converted the imported files from CP932 to UTF-8.
- Replaced Win32-only types and helpers with portable C++ equivalents.
- Kept the QR encoder algorithm intact for `Layer.drawQRCode(...)`.
