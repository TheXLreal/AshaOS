#include "sender_worker.h"

#include "protocol.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <time.h>

namespace {
quint64 monotonicNanoseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<quint64>(now.tv_sec) * 1000000000ULL + now.tv_nsec;
}

bool hasSignal(const QByteArray &bytes, int threshold) {
  const char *p = bytes.constData();
  for (int i = 0; i + 1 < bytes.size(); i += 2) {
    const quint16 value = static_cast<quint8>(p[i]) |
                          (static_cast<quint16>(static_cast<quint8>(p[i + 1])) << 8);
    const int sample = static_cast<qint16>(value);
    if (std::abs(sample) > threshold) return true;
  }
  return false;
}
}

SenderWorker::SenderWorker(QObject *parent) : QObject(parent) {}

QString SenderWorker::text(const char *russian, const char *english) const {
  return QString::fromUtf8(config_.russian ? russian : english);
}

void SenderWorker::fail(const QString &message) {
  emit logLine(message);
  stop();
  emit runningChanged(false, message);
}

void SenderWorker::start(const SenderConfig &config) {
  if (running_ || stopping_) return;
  config_ = config;
  if (!address_.setAddress(config.address) || address_.protocol() != QAbstractSocket::IPv4Protocol ||
      config.port == 0 || config.directBufferMs < 1 || config.directBufferMs > 300 ||
      config.prerollMs < 0 || config.prerollMs > 100 ||
      config.gateDbfs < -120 || config.gateDbfs > -20 ||
      config.maxOutputDbfs < -30 || config.maxOutputDbfs > 0 ||
      (config.directUdp && !config.direct)) {
    fail(text("Проверьте адрес и параметры подключения.",
              "Check the address and connection settings."));
    return;
  }

  nonce_.clear();
  if (config.direct) {
    QProcess adb;
    adb.start(QStringLiteral("adb"), {QStringLiteral("shell"),
                                      QStringLiteral("dumpsys"),
                                      QStringLiteral("bluetooth_manager")});
    if (!adb.waitForStarted(3000)) {
      fail(text("ADB не найден. Установите android-tools-adb и разрешите USB-отладку.",
                "ADB not found. Install android-tools-adb and authorize USB debugging."));
      return;
    }
    if (!adb.waitForFinished(10000)) {
      adb.kill();
      adb.waitForFinished();
      fail(text("ADB не ответил за 10 секунд.", "ADB did not respond within 10 seconds."));
      return;
    }
    if (adb.exitCode() != 0) {
      fail(text("ADB не читает Bluetooth-диагностику. Проверьте adb devices и разблокируйте телефон.",
                "ADB cannot read Bluetooth diagnostics. Check adb devices and unlock the phone."));
      return;
    }
    nonce_ = ashaos::parseSessionNonce(QString::fromUtf8(adb.readAllStandardOutput()));
    if (nonce_.size() != 16) {
      fail(text("Нет активного прямого ASHA-сеанса. Подключите аппарат и нажмите Start на телефоне.",
                "No active direct ASHA session. Connect the hearing aid and press Start on the phone."));
      return;
    }
    const QByteArray setup = ashaos::makeDirectConfig(nonce_, config.directBufferMs,
                                                      config.adaptive);
    if (config.directUdp) {
      udp_ = new QUdpSocket(this);
      if (udp_->writeDatagram(setup, address_, config.port) != setup.size()) {
        fail(text("Не удалось передать настройки буфера по UDP.",
                  "Could not send buffer configuration over UDP."));
        return;
      }
    } else {
      tcp_ = new QTcpSocket(this);
      tcp_->connectToHost(address_, config.port);
      if (!tcp_->waitForConnected(5000)) {
        const QString detail = tcp_->errorString();
        fail(text("Не удалось подключиться к телефону: ", "Cannot connect to the phone: ") + detail);
        return;
      }
      const int fd = static_cast<int>(tcp_->socketDescriptor());
      int enabled = 1;
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
      if (tcp_->write(setup) != setup.size()) {
        fail(text("Не удалось передать настройки буфера.",
                  "Could not send buffer configuration."));
        return;
      }
      tcp_->flush();
      connect(tcp_, &QTcpSocket::disconnected, this, [this] {
        if (running_ && !stopping_)
          fail(text("Телефон разорвал TCP-соединение.", "The phone closed the TCP connection."));
      });
    }
  } else {
    udp_ = new QUdpSocket(this);
  }

  const int rate = config.direct ? 16000 : 48000;
  capture_ = new QProcess(this);
  capture_->setProcessChannelMode(QProcess::SeparateChannels);
  connect(capture_, &QProcess::readyReadStandardOutput, this,
          &SenderWorker::captureReady);
  connect(capture_, &QProcess::finished, this, [this] {
    if (!stopping_) {
      const QString detail = QString::fromUtf8(capture_->readAllStandardError()).trimmed();
      fail(text("Захват системного звука остановился. ", "System audio capture stopped. ") + detail);
    }
  });
  capture_->start(QStringLiteral("parec"),
                  {QStringLiteral("--raw"),
                   QStringLiteral("--device=%1").arg(config.monitor),
                   QStringLiteral("--rate=%1").arg(rate),
                   QStringLiteral("--channels=2"), QStringLiteral("--format=s16le"),
                   QStringLiteral("--latency-msec=10")});
  if (!capture_->waitForStarted(3000)) {
    fail(text("Не запускается parec. Установите pulseaudio-utils.",
              "Cannot start parec. Install pulseaudio-utils."));
    return;
  }

  if (config.keepalive) {
    keepalive_ = new QProcess(this);
    keepalive_->start(QStringLiteral("pacat"),
                      {QStringLiteral("--playback"), QStringLiteral("--raw"),
                       QStringLiteral("--device=%1").arg(config.sink),
                       QStringLiteral("--rate=48000"),
                       QStringLiteral("--channels=2"), QStringLiteral("--format=s16le"),
                       QStringLiteral("--latency-msec=20")});
    if (!keepalive_->waitForStarted(3000)) {
      fail(text("Не запускается pacat. Установите pulseaudio-utils.",
                "Cannot start pacat. Install pulseaudio-utils."));
      return;
    }
    connect(keepalive_, &QProcess::finished, this, [this] {
      if (running_ && !stopping_)
        emit logLine(text("Режим удержания звукового выхода остановился.",
                          "Audio output keepalive stopped."));
    });
  }

  pcmQueue_.clear();
  clock_.start();
  lastSignalMs_ = -1;
  sequence_ = 0;
  sent_ = silent_ = dropped_ = 0;
  timer_ = new QTimer(this);
  timer_->setTimerType(Qt::PreciseTimer);
  connect(timer_, &QTimer::timeout, this, &SenderWorker::tick);
  timer_->start(config.direct ? 10 : 5);
  running_ = true;
  emit runningChanged(true, text("Передача активна", "Streaming is active"));
  emit logLine(text("Подключено. Тишина отправляется и до первого звука.",
                    "Connected. Silence is sent before the first sound, too."));
}

void SenderWorker::stop() {
  if (stopping_) return;
  stopping_ = true;
  if (timer_) {
    timer_->stop();
    delete timer_;
    timer_ = nullptr;
  }
  for (QProcess *&process : {std::ref(capture_), std::ref(keepalive_)}) {
    if (!process) continue;
    process->terminate();
    if (!process->waitForFinished(500)) {
      process->kill();
      process->waitForFinished(500);
    }
    delete process;
    process = nullptr;
  }
  if (tcp_) {
    tcp_->abort();
    delete tcp_;
    tcp_ = nullptr;
  }
  if (udp_) {
    delete udp_;
    udp_ = nullptr;
  }
  pcmQueue_.clear();
  nonce_.fill('\0');
  nonce_.clear();
  const bool wasRunning = running_;
  running_ = false;
  stopping_ = false;
  if (wasRunning) {
    emit logLine(text("Передача остановлена.", "Streaming stopped."));
    emit runningChanged(false, text("Остановлено", "Stopped"));
  }
}

void SenderWorker::captureReady() {
  if (!capture_ || !running_) return;
  const QByteArray data = capture_->readAllStandardOutput();
  if (data.isEmpty()) return;
  const int threshold = std::max(1, static_cast<int>(
      std::lround(32767.0 * std::pow(10.0, config_.gateDbfs / 20.0))));
  const qint64 now = clock_.elapsed();
  if (hasSignal(data, threshold)) lastSignalMs_ = now;
  if (config_.direct && (lastSignalMs_ < 0 || now - lastSignalMs_ > 500)) {
    pcmQueue_.clear();
    return;
  }
  pcmQueue_.append(data);
  const int frameBytes = config_.direct ? ashaos::kDirectPcmBytes : ashaos::kUdpPcmBytes;
  const int maxBytes = frameBytes * (config_.direct ? 32 : 64);
  if (pcmQueue_.size() > maxBytes) {
    const int remove = ((pcmQueue_.size() - maxBytes + frameBytes - 1) / frameBytes) * frameBytes;
    pcmQueue_.remove(0, remove);
    dropped_ += static_cast<quint64>(remove / frameBytes);
  }
}

QByteArray SenderWorker::processPcm(QByteArray pcm) const {
  const int threshold = std::max(1, static_cast<int>(
      std::lround(32767.0 * std::pow(10.0, config_.gateDbfs / 20.0))));
  if (!hasSignal(pcm, threshold)) {
    pcm.fill('\0');
    return pcm;
  }
  if (config_.maxOutputDbfs == 0) return pcm;
  const double gain = std::pow(10.0, config_.maxOutputDbfs / 20.0);
  char *p = pcm.data();
  for (int i = 0; i + 1 < pcm.size(); i += 2) {
    const quint16 bits = static_cast<quint8>(p[i]) |
                         (static_cast<quint16>(static_cast<quint8>(p[i + 1])) << 8);
    const int sample = static_cast<qint16>(bits);
    const int scaled = std::clamp(static_cast<int>(std::lround(sample * gain)), -32768, 32767);
    p[i] = static_cast<char>(scaled & 0xff);
    p[i + 1] = static_cast<char>((scaled >> 8) & 0xff);
  }
  return pcm;
}

void SenderWorker::tick() {
  if (!running_) return;
  if (keepalive_ && keepalive_->state() == QProcess::Running &&
      keepalive_->bytesToWrite() < 96000) {
    keepalive_->write(QByteArray(config_.direct ? 1920 : 960, '\0'));
  }
  const int frameBytes = config_.direct ? ashaos::kDirectPcmBytes : ashaos::kUdpPcmBytes;
  const int prerollPackets = config_.direct ? std::max(1, (config_.prerollMs + 9) / 10) : 1;
  QByteArray pcm(frameBytes, '\0');
  if (pcmQueue_.size() >= frameBytes * prerollPackets) {
    pcm = pcmQueue_.left(frameBytes);
    pcmQueue_.remove(0, frameBytes);
    pcm = processPcm(pcm);
  }
  if (pcm == QByteArray(frameBytes, '\0')) ++silent_;

  if (config_.direct) {
    const QByteArray record = ashaos::makeDirectPcm(sequence_, nonce_, pcm);
    if (config_.directUdp) {
      if (!udp_ || udp_->writeDatagram(record, address_, config_.port) != record.size()) {
        fail(text("Ошибка передачи Direct UDP.", "Direct UDP send failed."));
        return;
      }
    } else {
      if (!tcp_ || tcp_->state() != QAbstractSocket::ConnectedState) {
        fail(text("TCP-соединение потеряно.", "TCP connection lost."));
        return;
      }
      if (tcp_->bytesToWrite() > ashaos::kDirectRecordBytes * 8) {
        fail(text("Телефон не успевает принимать звук; соединение остановлено без накопления задержки.",
                  "The phone cannot keep up; stopped to prevent growing latency."));
        return;
      }
      if (tcp_->write(record) != record.size()) {
        fail(text("Ошибка передачи TCP.", "TCP send failed."));
        return;
      }
      tcp_->flush();
    }
  } else {
    const QByteArray packet = ashaos::makeUdpPacket(sequence_, monotonicNanoseconds(), pcm);
    if (!udp_ || udp_->writeDatagram(packet, address_, config_.port) != packet.size()) {
      fail(text("Ошибка передачи UDP.", "UDP send failed."));
      return;
    }
  }
  ++sequence_;
  ++sent_;
  if (sent_ % (config_.direct ? 100 : 200) == 0)
    emit statsChanged(sent_, silent_, dropped_);
}
