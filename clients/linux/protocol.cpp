#include "protocol.h"

#include <QRegularExpression>

namespace {
void put16(char *p, quint16 value) {
  p[0] = static_cast<char>(value & 0xff);
  p[1] = static_cast<char>((value >> 8) & 0xff);
}

void put32(char *p, quint32 value) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<char>((value >> (8 * i)) & 0xff);
}

void put64(char *p, quint64 value) {
  for (int i = 0; i < 8; ++i) p[i] = static_cast<char>((value >> (8 * i)) & 0xff);
}

QByteArray directHeader(quint32 sequence, const QByteArray &nonce,
                        quint16 frames, quint8 channels, quint8 type) {
  if (nonce.size() != 16) return {};
  QByteArray out(ashaos::kDirectRecordBytes, '\0');
  char *p = out.data();
  put32(p, 0x32444141);  // AAD2
  put16(p + 4, 2);
  put16(p + 6, 32);
  put32(p + 8, sequence);
  for (int i = 0; i < 16; ++i) p[12 + i] = nonce[i];
  put16(p + 28, frames);
  p[30] = static_cast<char>(channels);
  p[31] = static_cast<char>(type);
  return out;
}
}

namespace ashaos {
QByteArray parseSessionNonce(const QString &diagnostics) {
  const QRegularExpression pattern(
      QStringLiteral("Session nonce\\s*:\\s*([0-9a-fA-F]{32})(?![0-9a-fA-F])"));
  const auto match = pattern.match(diagnostics);
  if (!match.hasMatch()) return {};
  const QByteArray nonce = QByteArray::fromHex(match.captured(1).toLatin1());
  if (nonce.size() != 16 || nonce == QByteArray(16, '\0')) return {};
  return nonce;
}

QByteArray makeUdpPacket(quint32 sequence, quint64 monotonicNs,
                         const QByteArray &pcm) {
  if (pcm.size() != kUdpPcmBytes) return {};
  QByteArray out(kUdpPacketBytes, '\0');
  char *p = out.data();
  put32(p, 0x41485341);  // ASHA
  put16(p + 4, 1);
  put16(p + 6, 24);
  put32(p + 8, sequence);
  put64(p + 12, monotonicNs);
  put16(p + 20, 240);
  p[22] = 2;
  for (int i = 0; i < pcm.size(); ++i) p[24 + i] = pcm[i];
  return out;
}

QByteArray makeDirectConfig(const QByteArray &nonce, int bufferMs,
                            bool adaptive) {
  if (bufferMs < 1 || bufferMs > 300) return {};
  QByteArray out = directHeader(0, nonce, 0, 0, 1);
  if (out.isEmpty()) return out;
  put16(out.data() + 32, static_cast<quint16>(bufferMs));
  out[34] = adaptive ? 1 : 0;
  return out;
}

QByteArray makeDirectPcm(quint32 sequence, const QByteArray &nonce,
                         const QByteArray &pcm) {
  if (pcm.size() != kDirectPcmBytes) return {};
  QByteArray out = directHeader(sequence, nonce, 160, 2, 0);
  if (out.isEmpty()) return out;
  for (int i = 0; i < pcm.size(); ++i) out[32 + i] = pcm[i];
  return out;
}
}
