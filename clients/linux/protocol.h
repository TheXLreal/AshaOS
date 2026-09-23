#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace ashaos {
constexpr int kUdpPcmBytes = 960;
constexpr int kUdpPacketBytes = 984;
constexpr int kDirectPcmBytes = 640;
constexpr int kDirectRecordBytes = 672;

QByteArray parseSessionNonce(const QString &diagnostics);
QByteArray makeUdpPacket(quint32 sequence, quint64 monotonicNs,
                         const QByteArray &pcm);
QByteArray makeDirectConfig(const QByteArray &nonce, int bufferMs,
                            bool adaptive);
QByteArray makeDirectPcm(quint32 sequence, const QByteArray &nonce,
                         const QByteArray &pcm);
}
