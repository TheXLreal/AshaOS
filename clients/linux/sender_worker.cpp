#include "sender_worker.h"

#include "protocol.h"

#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QFileInfo>
#include <QNetworkProxy>
#include <QRegularExpression>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <system_error>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

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

int pcmSample(const char *bytes) {
  const quint16 bits = static_cast<quint8>(bytes[0]) |
                       (static_cast<quint16>(static_cast<quint8>(bytes[1])) << 8);
  return static_cast<qint16>(bits);
}

void putPcmSample(char *bytes, int sample) {
  bytes[0] = static_cast<char>(sample & 0xff);
  bytes[1] = static_cast<char>((sample >> 8) & 0xff);
}

void smoothPcmStart(QByteArray &pcm, const QByteArray &previous) {
  if (pcm.size() != previous.size() || pcm.size() < 64) return;
  constexpr int kBlendFrames = 16;
  for (int channel = 0; channel < 2; ++channel) {
    const int previousSample = pcmSample(previous.constData() + previous.size() - 4 + channel * 2);
    for (int frame = 0; frame < kBlendFrames; ++frame) {
      char *sample = pcm.data() + frame * 4 + channel * 2;
      const int original = pcmSample(sample);
      const int mixed = (previousSample * (kBlendFrames - frame - 1) +
                         original * (frame + 1)) / kBlendFrames;
      putPcmSample(sample, mixed);
    }
  }
}

void fadeRepeatedPcm(QByteArray &pcm, int missingPackets) {
  constexpr int kFadePackets = 5;
  if (missingPackets > kFadePackets) {
    pcm.fill('\0');
    return;
  }
  const int startPercent = 100 - (missingPackets - 1) * 20;
  const int endPercent = 100 - missingPackets * 20;
  const int frames = pcm.size() / 4;
  for (int frame = 0; frame < frames; ++frame) {
    const int percent = startPercent +
                        (endPercent - startPercent) * frame / (frames - 1);
    for (int channel = 0; channel < 2; ++channel) {
      char *sample = pcm.data() + frame * 4 + channel * 2;
      putPcmSample(sample, pcmSample(sample) * percent / 100);
    }
  }
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
      udp_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
      if (udp_->writeDatagram(setup, address_, config.port) != setup.size()) {
        fail(text("Не удалось передать настройки буфера по UDP.",
                  "Could not send buffer configuration over UDP."));
        return;
      }
    } else {
      tcp_ = new QTcpSocket(this);
      tcp_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
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
    udp_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
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
  primed_ = false;
  lastRealPcm_.clear();
  lastOutputPcm_.clear();
  missingPcmPackets_ = 0;
  sequence_ = 0;
  sent_ = 0;
  silent_ = 0;
  dropped_ = 0;
  timer_ = new QTimer(this);
  timer_->setTimerType(Qt::PreciseTimer);
  connect(timer_, &QTimer::timeout, this, &SenderWorker::tick);
  nextSendNs_ = monotonicNanoseconds() + (config.direct ? 10000000ULL : 5000000ULL);
  timer_->start(config.direct ? 10 : 5);
  running_ = true;
  if (config.direct && config.directUdp) {
    const int fd = dup(static_cast<int>(udp_->socketDescriptor()));
    if (fd < 0) {
      fail(text("Не удалось открыть сокет отправки звука.",
                "Could not open the audio sender socket."));
      return;
    }
    udpPacerStop_ = false;
    try {
      udpPacerThread_ = std::thread(&SenderWorker::runDirectUdpPacer, this, fd);
    } catch (const std::system_error &) {
      close(fd);
      fail(text("Не удалось запустить поток отправки звука.",
                "Could not start the audio sender thread."));
      return;
    }
  }
  emit runningChanged(true, text("Передача активна", "Streaming is active"));
  emit logLine(text("Подключено. Тишина отправляется и до первого звука.",
                    "Connected. Silence is sent before the first sound, too."));
}

void SenderWorker::stop() {
  if (stopping_) return;
  stopping_ = true;
  udpPacerStop_ = true;
  if (udpPacerThread_.joinable()) udpPacerThread_.join();
  if (timer_) {
    timer_->stop();
    timer_->disconnect(this);
    timer_->deleteLater();
    timer_ = nullptr;
  }
  for (QProcess *&process : {std::ref(capture_), std::ref(keepalive_)}) {
    if (!process) continue;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
      process->terminate();
      if (!process->waitForFinished(500)) {
        process->kill();
        process->waitForFinished(500);
      }
    }
    process->deleteLater();
    process = nullptr;
  }
  if (tcp_) {
    tcp_->disconnect(this);
    tcp_->abort();
    tcp_->deleteLater();
    tcp_ = nullptr;
  }
  if (udp_) {
    udp_->deleteLater();
    udp_ = nullptr;
  }
  pcmQueue_.clear();
  primed_ = false;
  lastRealPcm_.clear();
  lastOutputPcm_.clear();
  missingPcmPackets_ = 0;
  nextSendNs_ = 0;
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
  std::lock_guard<std::mutex> lock(pcmMutex_);
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
  const auto writeKeepalive = [this](quint64 packets) {
    if (!keepalive_ || keepalive_->state() != QProcess::Running) return;
    const int packetBytes = config_.direct ? 1920 : 960;
    const qint64 available = std::max<qint64>(0, 96000 - keepalive_->bytesToWrite());
    const qint64 count = std::min<qint64>(packets, available / packetBytes);
    if (count > 0)
      keepalive_->write(QByteArray(static_cast<int>(count * packetBytes), '\0'));
  };
  if (!config_.direct) {
    writeKeepalive(1);
    sendPacket();
    return;
  }

  // QTimer coalesces missed timeouts. Keep the packet count tied to elapsed
  // time so a delayed callback cannot permanently drain the phone's buffer.
  constexpr quint64 kPacketNs = 10000000ULL;
  constexpr quint64 kHalfPacketNs = kPacketNs / 2;
  constexpr quint64 kMaxBurst = 8;
  const quint64 now = monotonicNanoseconds();
  if (now + kHalfPacketNs < nextSendNs_) return;
  quint64 due = (now + kHalfPacketNs - nextSendNs_) / kPacketNs + 1;
  if (due > kMaxBurst) {
    due = kMaxBurst;
    nextSendNs_ = now + kHalfPacketNs - (kMaxBurst - 1) * kPacketNs;
  }
  writeKeepalive(due);
  if (config_.directUdp) {
    nextSendNs_ += due * kPacketNs;
    return;
  }
  for (quint64 i = 0; i < due; ++i) {
    if (!sendPacket()) return;
    nextSendNs_ += kPacketNs;
  }
}

QByteArray SenderWorker::nextPcm() {
  const int frameBytes = config_.direct ? ashaos::kDirectPcmBytes : ashaos::kUdpPcmBytes;
  const int prerollPackets = config_.direct ? std::max(1, (config_.prerollMs + 9) / 10) : 1;
  QByteArray pcm(frameBytes, '\0');
  {
    std::lock_guard<std::mutex> lock(pcmMutex_);
    if (!primed_ && pcmQueue_.size() >= frameBytes * prerollPackets)
      primed_ = true;
    if (primed_ && pcmQueue_.size() >= frameBytes) {
      pcm = pcmQueue_.left(frameBytes);
      pcmQueue_.remove(0, frameBytes);
      pcm = processPcm(pcm);
      if (missingPcmPackets_ > 0)
        smoothPcmStart(pcm, lastOutputPcm_);
      lastRealPcm_ = pcm;
      missingPcmPackets_ = 0;
    } else if (primed_ && lastRealPcm_.size() == frameBytes) {
      // A late capture chunk should not turn one missing frame into a full
      // preroll of silence. Conceal briefly and resume on the next real frame.
      pcm = lastRealPcm_;
      ++missingPcmPackets_;
      fadeRepeatedPcm(pcm, missingPcmPackets_);
      smoothPcmStart(pcm, lastOutputPcm_);
    }
    if (primed_) lastOutputPcm_ = pcm;
  }
  if (pcm == QByteArray(frameBytes, '\0')) ++silent_;
  return pcm;
}

void SenderWorker::runDirectUdpPacer(int fd) {
  // Match Windows' MMCSS sender priority through the Linux audio scheduler.
  // RTKit requires a bounded real-time CPU limit for the requesting process.
  rlimit cpuLimit{};
  if (getrlimit(RLIMIT_RTTIME, &cpuLimit) == 0 &&
      (cpuLimit.rlim_cur > 200000 || cpuLimit.rlim_max > 200000)) {
    cpuLimit.rlim_cur = std::min<rlim_t>(cpuLimit.rlim_cur, 200000);
    cpuLimit.rlim_max = std::min<rlim_t>(cpuLimit.rlim_max, 200000);
    setrlimit(RLIMIT_RTTIME, &cpuLimit);
  }
  QDBusMessage priorityRequest = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.RealtimeKit1"),
      QStringLiteral("/org/freedesktop/RealtimeKit1"),
      QStringLiteral("org.freedesktop.RealtimeKit1"),
      QStringLiteral("MakeThreadRealtime"));
  priorityRequest << static_cast<qulonglong>(syscall(SYS_gettid))
                  << static_cast<quint32>(10);
  const QDBusMessage priorityReply = QDBusConnection::systemBus().call(
      priorityRequest, QDBus::Block, 3000);
  if (priorityReply.type() == QDBusMessage::ReplyMessage) {
    emit logLine(text("Отправка звука работает с аудиоприоритетом RTKit.",
                      "Audio sender has RTKit audio priority."));
  } else {
    emit logLine(text("Аудиоприоритет RTKit недоступен; используется обычный приоритет. ",
                      "RTKit audio priority unavailable; using normal priority. ") +
                 priorityReply.errorMessage());
  }

  sockaddr_in target{};
  target.sin_family = AF_INET;
  target.sin_port = htons(config_.port);
  const QByteArray address = config_.address.toLatin1();
  if (inet_pton(AF_INET, address.constData(), &target.sin_addr) != 1) {
    close(fd);
    QMetaObject::invokeMethod(this, [this] {
      if (running_) fail(text("Неверный адрес телефона.", "Invalid phone address."));
    }, Qt::QueuedConnection);
    return;
  }

  constexpr quint64 kPacketNs = 10000000ULL;
  constexpr quint64 kMaxBurst = 8;
  quint64 next = monotonicNanoseconds() + kPacketNs;
  while (!udpPacerStop_) {
    const timespec deadline{static_cast<time_t>(next / 1000000000ULL),
                            static_cast<long>(next % 1000000000ULL)};
    int waitResult;
    do {
      waitResult = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr);
    } while (waitResult == EINTR && !udpPacerStop_);
    if (udpPacerStop_) break;
    if (waitResult != 0) {
      QMetaObject::invokeMethod(this, [this] {
        if (running_) fail(text("Таймер отправки звука остановился.",
                                "Audio sender timer stopped."));
      }, Qt::QueuedConnection);
      break;
    }

    const quint64 now = monotonicNanoseconds();
    quint64 due = now >= next ? (now - next) / kPacketNs + 1 : 1;
    if (due > kMaxBurst) {
      due = kMaxBurst;
      next = now - (kMaxBurst - 1) * kPacketNs;
    }
    for (quint64 i = 0; i < due && !udpPacerStop_; ++i) {
      const QByteArray record = ashaos::makeDirectPcm(sequence_, nonce_, nextPcm());
      ssize_t written;
      do {
        written = sendto(fd, record.constData(), record.size(), MSG_DONTWAIT,
                         reinterpret_cast<const sockaddr *>(&target), sizeof(target));
      } while (written < 0 && errno == EINTR);
      if (written != record.size()) {
        QMetaObject::invokeMethod(this, [this] {
          if (running_) fail(text("Ошибка передачи Direct UDP.", "Direct UDP send failed."));
        }, Qt::QueuedConnection);
        close(fd);
        return;
      }
      ++sequence_;
      const quint64 sent = ++sent_;
      if (sent % 100 == 0)
        emit statsChanged(sent, silent_.load(), dropped_.load());
      next += kPacketNs;
    }
  }
  close(fd);
}

bool SenderWorker::sendPacket() {
  const QByteArray pcm = nextPcm();

  if (config_.direct) {
    const QByteArray record = ashaos::makeDirectPcm(sequence_, nonce_, pcm);
    if (config_.directUdp) {
      if (!udp_ || udp_->writeDatagram(record, address_, config_.port) != record.size()) {
        fail(text("Ошибка передачи Direct UDP.", "Direct UDP send failed."));
        return false;
      }
    } else {
      if (!tcp_ || tcp_->state() != QAbstractSocket::ConnectedState) {
        fail(text("TCP-соединение потеряно.", "TCP connection lost."));
        return false;
      }
      if (tcp_->bytesToWrite() > ashaos::kDirectRecordBytes * 8) {
        fail(text("Телефон не успевает принимать звук; соединение остановлено без накопления задержки.",
                  "The phone cannot keep up; stopped to prevent growing latency."));
        return false;
      }
      if (tcp_->write(record) != record.size()) {
        fail(text("Ошибка передачи TCP.", "TCP send failed."));
        return false;
      }
      tcp_->flush();
    }
  } else {
    const QByteArray packet = ashaos::makeUdpPacket(sequence_, monotonicNanoseconds(), pcm);
    if (!udp_ || udp_->writeDatagram(packet, address_, config_.port) != packet.size()) {
      fail(text("Ошибка передачи UDP.", "UDP send failed."));
      return false;
    }
  }
  ++sequence_;
  ++sent_;
  if (sent_ % (config_.direct ? 100 : 200) == 0)
    emit statsChanged(sent_, silent_, dropped_);
  return true;
}
