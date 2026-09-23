#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

#include <atomic>
#include <mutex>
#include <thread>

struct SenderConfig {
  QString address;
  quint16 port = 48101;
  QString monitor = QStringLiteral("@DEFAULT_MONITOR@");
  QString sink = QStringLiteral("@DEFAULT_SINK@");
  bool direct = true;
  bool directUdp = false;
  bool keepalive = true;
  bool adaptive = false;
  bool russian = true;
  int directBufferMs = 20;
  int prerollMs = 40;
  int gateDbfs = -80;
  int maxOutputDbfs = 0;
};

class SenderWorker : public QObject {
  Q_OBJECT
 public:
  explicit SenderWorker(QObject *parent = nullptr);

 public slots:
  void start(const SenderConfig &config);
  void stop();

 signals:
  void runningChanged(bool running, const QString &message);
  void logLine(const QString &line);
  void statsChanged(quint64 sent, quint64 silent, quint64 dropped);

 private:
  void tick();
  bool sendPacket();
  QByteArray nextPcm();
  void runDirectUdpPacer(int fd);
  void captureReady();
  void fail(const QString &message);
  QByteArray processPcm(QByteArray pcm) const;
  QString text(const char *russian, const char *english) const;

  SenderConfig config_;
  QProcess *capture_ = nullptr;
  QProcess *keepalive_ = nullptr;
  QTcpSocket *tcp_ = nullptr;
  QUdpSocket *udp_ = nullptr;
  QTimer *timer_ = nullptr;
  QHostAddress address_;
  QByteArray nonce_;
  QByteArray pcmQueue_;
  QByteArray lastRealPcm_;
  QByteArray lastOutputPcm_;
  int missingPcmPackets_ = 0;
  std::mutex pcmMutex_;
  std::thread udpPacerThread_;
  std::atomic<bool> udpPacerStop_{false};
  quint32 sequence_ = 0;
  quint64 nextSendNs_ = 0;
  std::atomic<quint64> sent_{0};
  std::atomic<quint64> silent_{0};
  std::atomic<quint64> dropped_{0};
  bool running_ = false;
  bool stopping_ = false;
  bool primed_ = false;
};
