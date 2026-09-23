#include "sender_worker.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTextDocument>
#include <QThread>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QLabel *makeLabel(const QString &value = {}) {
  auto *label = new QLabel(value);
  label->setWordWrap(true);
  return label;
}

QFrame *makeCard() {
  auto *frame = new QFrame;
  frame->setObjectName(QStringLiteral("card"));
  return frame;
}

QWidget *helpLabel(QLabel *label, QToolButton **button) {
  auto *row = new QWidget;
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);
  layout->addWidget(label);
  *button = new QToolButton;
  (*button)->setObjectName(QStringLiteral("help"));
  (*button)->setText(QStringLiteral("?"));
  (*button)->setFixedSize(21, 21);
  (*button)->setCursor(Qt::PointingHandCursor);
  layout->addWidget(*button);
  layout->addStretch();
  QObject::connect(*button, &QToolButton::clicked, *button, [button] {
    QMessageBox::information(*button, QStringLiteral("AshaOS"), (*button)->toolTip());
  });
  return row;
}

QSpinBox *makeSpin(int minimum, int maximum, int value, const QString &suffix = {}) {
  auto *spin = new QSpinBox;
  spin->setRange(minimum, maximum);
  spin->setValue(value);
  spin->setSuffix(suffix);
  return spin;
}
}

class MainWindow : public QMainWindow {
 public:
  MainWindow() : settings_(QStringLiteral("AshaOS"), QStringLiteral("LinuxSender")) {
    setWindowTitle(QStringLiteral("AshaOS 1.7 · Linux"));
    setWindowIcon(QIcon(QStringLiteral(":/ashaos.png")));
    resize(790, 720);
    setMinimumSize(590, 470);
    QApplication::setQuitOnLastWindowClosed(false);
    basePalette_ = qApp->palette();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *root = new QWidget;
    auto *stack = new QVBoxLayout(root);
    stack->setContentsMargins(22, 20, 22, 22);
    stack->setSpacing(16);

    auto *header = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(QIcon(QStringLiteral(":/ashaos.png")).pixmap(54, 54));
    icon->setFixedSize(58, 58);
    header->addWidget(icon);
    auto *headerText = new QVBoxLayout;
    auto *title = makeLabel(QStringLiteral("AshaOS 1.7"));
    title->setObjectName(QStringLiteral("title"));
    headerText->addWidget(title);
    subtitle_ = makeLabel();
    subtitle_->setObjectName(QStringLiteral("subtitle"));
    headerText->addWidget(subtitle_);
    header->addLayout(headerText, 1);
    language_ = new QComboBox;
    language_->addItems({QStringLiteral("Русский"), QStringLiteral("English")});
    header->addWidget(language_);
    theme_ = new QComboBox;
    theme_->addItems({QStringLiteral("Системная"), QStringLiteral("Тёмная"),
                      QStringLiteral("Светлая")});
    header->addWidget(theme_);
    stack->addLayout(header);

    auto *connectionCard = makeCard();
    auto *connection = new QGridLayout(connectionCard);
    connection->setContentsMargins(18, 18, 18, 18);
    connection->setHorizontalSpacing(12);
    connection->setVerticalSpacing(12);
    connectionTitle_ = makeLabel();
    connectionTitle_->setObjectName(QStringLiteral("sectionTitle"));
    connection->addWidget(connectionTitle_, 0, 0, 1, 4);
    modeLabel_ = makeLabel();
    connection->addWidget(modeLabel_, 1, 0);
    mode_ = new QComboBox;
    mode_->addItems({QStringLiteral("Wi-Fi — UDP / AudioTrack"),
                     QStringLiteral("USB — UDP / AudioTrack"),
                     QStringLiteral("Bluetooth PAN — UDP / AudioTrack"),
                     QStringLiteral("Direct USB — TCP"),
                     QStringLiteral("Direct USB — UDP"),
                     QStringLiteral("Direct Wi-Fi — TCP"),
                     QStringLiteral("Direct Wi-Fi — UDP"),
                     QStringLiteral("Direct Bluetooth PAN — TCP"),
                     QStringLiteral("Direct Bluetooth PAN — UDP")});
    mode_->setCurrentIndex(3);
    connection->addWidget(mode_, 1, 1, 1, 3);
    addressLabel_ = makeLabel();
    connection->addWidget(addressLabel_, 2, 0);
    address_ = new QLineEdit;
    address_->setPlaceholderText(QStringLiteral("192.168.42.129"));
    connection->addWidget(address_, 2, 1, 1, 2);
    port_ = makeSpin(1, 65535, 48101);
    connection->addWidget(port_, 2, 3);
    outputLabel_ = makeLabel();
    connection->addWidget(outputLabel_, 3, 0);
    output_ = new QComboBox;
    output_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connection->addWidget(output_, 3, 1, 1, 2);
    refresh_ = new QPushButton;
    connection->addWidget(refresh_, 3, 3);
    connectionHint_ = makeLabel();
    connectionHint_->setObjectName(QStringLiteral("muted"));
    connection->addWidget(connectionHint_, 4, 0, 1, 4);
    connection->setColumnStretch(2, 1);
    stack->addWidget(connectionCard);

    auto *audioCard = makeCard();
    auto *audio = new QGridLayout(audioCard);
    audio->setContentsMargins(18, 18, 18, 18);
    audio->setHorizontalSpacing(12);
    audio->setVerticalSpacing(12);
    audioTitle_ = makeLabel();
    audioTitle_->setObjectName(QStringLiteral("sectionTitle"));
    audio->addWidget(audioTitle_, 0, 0, 1, 2);
    keepalive_ = new QCheckBox;
    keepalive_->setChecked(true);
    audio->addWidget(keepalive_, 1, 0, 1, 2);
    bufferLabel_ = makeLabel();
    audio->addWidget(helpLabel(bufferLabel_, &bufferHelp_), 2, 0);
    buffer_ = makeSpin(1, 300, 20, QStringLiteral(" ms"));
    audio->addWidget(buffer_, 2, 1);
    prerollLabel_ = makeLabel();
    audio->addWidget(helpLabel(prerollLabel_, &prerollHelp_), 3, 0);
    preroll_ = makeSpin(0, 100, 40, QStringLiteral(" ms"));
    preroll_->setSingleStep(10);
    audio->addWidget(preroll_, 3, 1);
    gateLabel_ = makeLabel();
    audio->addWidget(helpLabel(gateLabel_, &gateHelp_), 4, 0);
    gate_ = makeSpin(-120, -20, -80, QStringLiteral(" dBFS"));
    audio->addWidget(gate_, 4, 1);
    audio->setColumnStretch(0, 1);
    stack->addWidget(audioCard);

    developers_ = new QToolButton;
    developers_->setCheckable(true);
    developers_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    developers_->setArrowType(Qt::RightArrow);
    stack->addWidget(developers_, 0, Qt::AlignLeft);
    developerCard_ = makeCard();
    auto *developer = new QGridLayout(developerCard_);
    developer->setContentsMargins(18, 18, 18, 18);
    developer->setHorizontalSpacing(12);
    developer->setVerticalSpacing(12);
    adaptive_ = new QCheckBox;
    developer->addWidget(adaptive_, 0, 0, 1, 2);
    outputDbLabel_ = makeLabel();
    developer->addWidget(helpLabel(outputDbLabel_, &outputDbHelp_), 1, 0);
    outputDb_ = makeSpin(-30, 0, 0, QStringLiteral(" dBFS"));
    developer->addWidget(outputDb_, 1, 1);
    monitorLabel_ = makeLabel();
    developer->addWidget(helpLabel(monitorLabel_, &monitorHelp_), 2, 0);
    monitorOverride_ = new QLineEdit;
    developer->addWidget(monitorOverride_, 2, 1);
    logsTitle_ = makeLabel();
    logsTitle_->setObjectName(QStringLiteral("sectionTitle"));
    developer->addWidget(logsTitle_, 3, 0, 1, 2);
    logs_ = new QPlainTextEdit;
    logs_->setReadOnly(true);
    logs_->document()->setMaximumBlockCount(400);
    logs_->setMinimumHeight(145);
    developer->addWidget(logs_, 4, 0, 1, 2);
    stack->addWidget(developerCard_);

    auto *bottom = new QHBoxLayout;
    status_ = makeLabel();
    status_->setObjectName(QStringLiteral("status"));
    bottom->addWidget(status_, 1);
    start_ = new QPushButton;
    start_->setObjectName(QStringLiteral("primary"));
    start_->setMinimumWidth(135);
    bottom->addWidget(start_);
    quit_ = new QPushButton;
    bottom->addWidget(quit_);
    stack->addLayout(bottom);
    stats_ = makeLabel();
    stats_->setObjectName(QStringLiteral("muted"));
    stack->addWidget(stats_);
    stack->addStretch();
    scroll->setWidget(root);
    setCentralWidget(scroll);

    worker_ = new SenderWorker;
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &SenderWorker::runningChanged, this,
            [this](bool running, const QString &message) {
              running_ = running;
              start_->setEnabled(true);
              start_->setText(running ? trText("Стоп", "Stop") : trText("Старт", "Start"));
              status_->setText(message);
              mode_->setEnabled(!running);
              address_->setEnabled(!running);
              port_->setEnabled(!running);
              output_->setEnabled(!running);
              refresh_->setEnabled(!running);
              if (tray_) tray_->setToolTip(QStringLiteral("AshaOS · ") + message);
            });
    connect(worker_, &SenderWorker::logLine, this, [this](const QString &line) {
      logs_->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) +
                             QStringLiteral("  ") + line);
    });
    connect(worker_, &SenderWorker::statsChanged, this,
            [this](quint64 sent, quint64 silent, quint64 dropped) {
              stats_->setText(trText("Пакетов: %1 · тишина: %2 · отброшено: %3",
                                     "Packets: %1 · silence: %2 · dropped: %3")
                                  .arg(sent).arg(silent).arg(dropped));
            });
    workerThread_.start();

    connect(start_, &QPushButton::clicked, this, [this] {
      if (running_) {
        start_->setEnabled(false);
        QMetaObject::invokeMethod(worker_, [this] { worker_->stop(); }, Qt::QueuedConnection);
      } else {
        saveSettings();
        SenderConfig config;
        config.address = address_->text().trimmed();
        config.port = static_cast<quint16>(port_->value());
        config.direct = mode_->currentIndex() >= 3;
        config.directUdp = mode_->currentIndex() == 4 ||
                           mode_->currentIndex() == 6 ||
                           mode_->currentIndex() == 8;
        config.sink = output_->currentData().toString();
        if (config.sink.isEmpty()) config.sink = QStringLiteral("@DEFAULT_SINK@");
        config.monitor = monitorOverride_->text().trimmed();
        if (config.monitor.isEmpty()) {
          config.monitor = config.sink == QStringLiteral("@DEFAULT_SINK@")
                               ? QStringLiteral("@DEFAULT_MONITOR@")
                               : config.sink + QStringLiteral(".monitor");
        }
        config.keepalive = keepalive_->isChecked();
        config.adaptive = adaptive_->isChecked();
        config.russian = language_->currentIndex() == 0;
        config.directBufferMs = buffer_->value();
        config.prerollMs = preroll_->value();
        config.gateDbfs = gate_->value();
        config.maxOutputDbfs = outputDb_->value();
        start_->setEnabled(false);
        status_->setText(trText("Подключение…", "Connecting…"));
        QMetaObject::invokeMethod(worker_, [this, config] { worker_->start(config); },
                                  Qt::QueuedConnection);
      }
    });
    connect(quit_, &QPushButton::clicked, this, &MainWindow::quitCompletely);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int index) {
      if (index >= 3 && port_->value() == 48100) port_->setValue(48101);
      if (index < 3 && port_->value() == 48101) port_->setValue(48100);
      updateMode();
    });
    connect(refresh_, &QPushButton::clicked, this, &MainWindow::refreshOutputs);
    connect(developers_, &QToolButton::toggled, this, [this](bool open) {
      developerCard_->setVisible(open);
      developers_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(language_, &QComboBox::currentIndexChanged, this, [this] { retranslate(); });
    connect(theme_, &QComboBox::currentIndexChanged, this, [this] { applyTheme(); });
    connect(buffer_, &QSpinBox::valueChanged, this, [this] { updateAudioHelp(); });
    connect(preroll_, &QSpinBox::valueChanged, this, [this] { updateAudioHelp(); });

    tray_ = new QSystemTrayIcon(QIcon(QStringLiteral(":/ashaos.png")), this);
    trayMenu_ = new QMenu(this);
    trayOpen_ = trayMenu_->addAction(QString());
    trayQuit_ = trayMenu_->addAction(QString());
    connect(trayOpen_, &QAction::triggered, this, [this] {
      showNormal();
      raise();
      activateWindow();
    });
    connect(trayQuit_, &QAction::triggered, this, &MainWindow::quitCompletely);
    tray_->setContextMenu(trayMenu_);
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
              if (reason == QSystemTrayIcon::DoubleClick ||
                  reason == QSystemTrayIcon::Trigger) {
                showNormal();
                raise();
                activateWindow();
              }
            });
    if (QSystemTrayIcon::isSystemTrayAvailable()) tray_->show();

    loadSettings();
    refreshOutputs();
    const int sinkIndex = output_->findData(selectedSink_);
    if (sinkIndex >= 0) output_->setCurrentIndex(sinkIndex);
    developerCard_->setVisible(false);
    retranslate();
    applyTheme();
  }

  ~MainWindow() override {
    saveSettings();
    if (workerThread_.isRunning()) {
      QMetaObject::invokeMethod(worker_, [this] { worker_->stop(); },
                                Qt::BlockingQueuedConnection);
      workerThread_.quit();
      workerThread_.wait();
    }
  }

 protected:
  void closeEvent(QCloseEvent *event) override {
    if (exiting_) {
      event->accept();
      return;
    }
    event->ignore();
    if (tray_->isVisible()) hide();
    else showMinimized();
  }

 private:
  QString trText(const char *ru, const char *en) const {
    return QString::fromUtf8(language_->currentIndex() == 0 ? ru : en);
  }

  void updateAudioHelp() {
    bufferHelp_->setToolTip(trText(
        "Буфер телефона сейчас %1 мс. Меньше — ниже задержка, но выше риск прерываний.",
        "Phone buffer is now %1 ms. Lower is faster but can stutter.")
        .arg(buffer_->value()));
    prerollHelp_->setToolTip(trText(
        "Предбуфер сейчас %1 мс. ПК собирает звук до отправки первого кадра. "
        "При 0 мс всё равно нужен один полный кадр 10 мс. Больший запас добавляет задержку.",
        "Sender preroll is now %1 ms. The PC collects audio before sending. "
        "At 0 ms, one full 10 ms record is still required. More adds delay.")
        .arg(preroll_->value()));
  }

  void quitCompletely() {
    exiting_ = true;
    tray_->hide();
    close();
    qApp->quit();
  }

  void refreshOutputs() {
    const QString selected = output_->currentData().toString();
    output_->clear();
    output_->addItem(trText("Системный выход", "System default output"),
                     QStringLiteral("@DEFAULT_SINK@"));
    QProcess pactl;
    pactl.start(QStringLiteral("pactl"), {QStringLiteral("list"),
                                         QStringLiteral("short"), QStringLiteral("sinks")});
    if (pactl.waitForStarted(1000) && pactl.waitForFinished(2500) &&
        pactl.exitCode() == 0) {
      const QStringList lines = QString::fromUtf8(pactl.readAllStandardOutput())
                                    .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
      for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() >= 2) output_->addItem(fields[1], fields[1]);
      }
    } else if (pactl.state() != QProcess::NotRunning) {
      pactl.kill();
      pactl.waitForFinished();
    }
    const int restored = output_->findData(selected.isEmpty() ?
                                             QStringLiteral("@DEFAULT_SINK@") : selected);
    output_->setCurrentIndex(restored < 0 ? 0 : restored);
  }

  void updateMode() {
    const int mode = mode_->currentIndex();
    const bool direct = mode >= 3;
    buffer_->setEnabled(direct);
    preroll_->setEnabled(direct);
    adaptive_->setEnabled(direct);
    const QString networkHint = (mode == 0 || mode == 5 || mode == 6)
        ? trText("Подключите ПК и телефон к одной сети Wi-Fi.",
                 "Connect the PC and phone to the same Wi-Fi network.")
        : (mode == 1 || mode == 3 || mode == 4)
            ? trText("Подключите USB и включите USB-модем на телефоне.",
                     "Connect USB and enable USB tethering on the phone.")
            : trText("Включите Bluetooth-модем и подключите ПК к телефону через PAN.",
                     "Enable Bluetooth tethering and connect the PC to the phone through PAN.");
    connectionHint_->setText(networkHint + QStringLiteral(" ") +
        (direct
            ? trText("Для Direct нужен разрешённый ADB: он читает ключ сеанса.",
                     "Direct needs authorized ADB to read the session key.")
            : trText("Выберите тот же режим на телефоне и скопируйте его IP:порт.",
                     "Choose the same mode on the phone and copy its IP:port.")));
  }

  void retranslate() {
    subtitle_->setText(trText("Звук Linux → AshaOS → слуховой аппарат",
                              "Linux audio → AshaOS → hearing aid"));
    const int selectedTheme = theme_->currentIndex();
    theme_->setItemText(0, trText("Системная", "System"));
    theme_->setItemText(1, trText("Тёмная", "Dark"));
    theme_->setItemText(2, trText("Светлая", "Light"));
    theme_->setCurrentIndex(selectedTheme);
    connectionTitle_->setText(trText("Подключение", "Connection"));
    modeLabel_->setText(trText("Режим", "Mode"));
    addressLabel_->setText(trText("Адрес телефона", "Phone address"));
    outputLabel_->setText(trText("Звуковой выход", "Audio output"));
    refresh_->setText(trText("Обновить", "Refresh"));
    audioTitle_->setText(trText("Звук и задержка", "Audio and latency"));
    keepalive_->setText(trText("Держать системный звук активным", "Keep desktop audio active"));
    bufferLabel_->setText(trText("Буфер телефона", "Phone buffer"));
    prerollLabel_->setText(trText("Предбуфер", "Sender preroll"));
    gateLabel_->setText(trText("Порог тишины", "Silence threshold"));
    developers_->setText(trText("Для разработчиков", "For developers"));
    adaptive_->setText(trText("Адаптивный буфер телефона", "Adaptive phone buffer"));
    outputDbLabel_->setText(trText("Макс. громкость", "Max output level"));
    monitorLabel_->setText(trText("Источник монитора вручную", "Monitor source override"));
    logsTitle_->setText(trText("Логи", "Logs"));
    start_->setText(running_ ? trText("Стоп", "Stop") : trText("Старт", "Start"));
    quit_->setText(trText("Выход", "Quit"));
    if (!running_) status_->setText(trText("Готов к подключению", "Ready to connect"));
    if (stats_->text().isEmpty())
      stats_->setText(trText("Сначала запустите приём на телефоне.",
                             "Start listening on the phone first."));
    trayOpen_->setText(trText("Открыть AshaOS", "Open AshaOS"));
    trayQuit_->setText(trText("Выйти полностью", "Quit completely"));
    updateAudioHelp();
    gateHelp_->setToolTip(trText(
        "Сигнал тише этого уровня считается тишиной. По умолчанию −80 dBFS; это не регулятор громкости слухового аппарата.",
        "Audio below this level is treated as silence. Default −80 dBFS; this is not hearing-aid volume."));
    outputDbHelp_->setToolTip(trText(
        "Только ослабляет PCM перед отправкой. 0 dBFS оставляет исходный уровень.",
        "Only attenuates PCM before sending. 0 dBFS keeps the original level."));
    monitorHelp_->setToolTip(trText(
        "Оставьте пустым для выбранного выхода. Если у выхода нестандартное имя монитора, укажите его из pactl list short sources.",
        "Leave blank for the selected output. For a custom monitor name, use pactl list short sources."));
    updateMode();
  }

  void applyTheme() {
    const bool dark = theme_->currentIndex() == 1 ||
        (theme_->currentIndex() == 0 && basePalette_.color(QPalette::Window).lightness() < 128);
    QPalette palette = basePalette_;
    const QString bg = dark ? QStringLiteral("#10151d") : QStringLiteral("#f2f5f9");
    const QString card = dark ? QStringLiteral("#1b2430") : QStringLiteral("#ffffff");
    const QString border = dark ? QStringLiteral("#344354") : QStringLiteral("#d9e2ec");
    const QString text = dark ? QStringLiteral("#ecf3fc") : QStringLiteral("#172437");
    const QString muted = dark ? QStringLiteral("#acbacb") : QStringLiteral("#5b6b7d");
    palette.setColor(QPalette::Window, QColor(bg));
    palette.setColor(QPalette::Base, QColor(card));
    palette.setColor(QPalette::Text, QColor(text));
    palette.setColor(QPalette::WindowText, QColor(text));
    palette.setColor(QPalette::ButtonText, QColor(text));
    qApp->setPalette(palette);
    qApp->setStyleSheet(QStringLiteral(
        "QWidget { color: %1; font-size: 10pt; }"
        "QMainWindow, QScrollArea, QScrollArea > QWidget > QWidget { background: %2; }"
        "QFrame#card { background: %3; border: 1px solid %4; border-radius: 15px; }"
        "QLabel#title { font-size: 21pt; font-weight: 750; }"
        "QLabel#sectionTitle { font-size: 12pt; font-weight: 700; }"
        "QLabel#muted, QLabel#subtitle { color: %5; }"
        "QLabel#status { font-weight: 700; }"
        "QLineEdit, QSpinBox, QComboBox, QPlainTextEdit { background: %3; border: 1px solid %4; border-radius: 7px; padding: 6px; }"
        "QPushButton, QToolButton { background: %3; border: 1px solid %4; border-radius: 8px; padding: 7px 10px; }"
        "QPushButton:hover, QToolButton:hover { border-color: #4da8d8; }"
        "QPushButton#primary { background: #176f9c; color: white; border-color: #176f9c; font-weight: 700; }"
        "QPushButton#primary:hover { background: #2789b8; }"
        "QToolButton#help { border-radius: 10px; padding: 0; background: #2b80aa; color: white; font-weight: 700; }"
    ).arg(text, bg, card, border, muted));
  }

  void loadSettings() {
    const QByteArray geometry = settings_.value(QStringLiteral("geometry")).toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    address_->setText(settings_.value(QStringLiteral("address")).toString());
    int savedMode = settings_.value(QStringLiteral("mode"), 0).toInt();
    if (settings_.value(QStringLiteral("mode_version"), 0).toInt() < 2)
      savedMode = savedMode == 0 ? 3 : 0;
    mode_->setCurrentIndex(std::clamp(savedMode, 0, 8));
    port_->setValue(settings_.value(QStringLiteral("port"), mode_->currentIndex() >= 3 ? 48101 : 48100).toInt());
    language_->setCurrentIndex(settings_.value(QStringLiteral("language"), 0).toInt());
    theme_->setCurrentIndex(settings_.value(QStringLiteral("theme"), 0).toInt());
    keepalive_->setChecked(settings_.value(QStringLiteral("keepalive"), true).toBool());
    buffer_->setValue(settings_.value(QStringLiteral("buffer"), 20).toInt());
    preroll_->setValue(settings_.value(QStringLiteral("preroll"), 40).toInt());
    gate_->setValue(settings_.value(QStringLiteral("gate"), -80).toInt());
    adaptive_->setChecked(settings_.value(QStringLiteral("adaptive"), false).toBool());
    outputDb_->setValue(settings_.value(QStringLiteral("output_db"), 0).toInt());
    monitorOverride_->setText(settings_.value(QStringLiteral("monitor_override")).toString());
    selectedSink_ = settings_.value(QStringLiteral("sink"), QStringLiteral("@DEFAULT_SINK@")).toString();
  }

  void saveSettings() {
    settings_.setValue(QStringLiteral("geometry"), saveGeometry());
    settings_.setValue(QStringLiteral("address"), address_->text());
    settings_.setValue(QStringLiteral("mode"), mode_->currentIndex());
    settings_.setValue(QStringLiteral("mode_version"), 2);
    settings_.setValue(QStringLiteral("port"), port_->value());
    settings_.setValue(QStringLiteral("language"), language_->currentIndex());
    settings_.setValue(QStringLiteral("theme"), theme_->currentIndex());
    settings_.setValue(QStringLiteral("keepalive"), keepalive_->isChecked());
    settings_.setValue(QStringLiteral("buffer"), buffer_->value());
    settings_.setValue(QStringLiteral("preroll"), preroll_->value());
    settings_.setValue(QStringLiteral("gate"), gate_->value());
    settings_.setValue(QStringLiteral("adaptive"), adaptive_->isChecked());
    settings_.setValue(QStringLiteral("output_db"), outputDb_->value());
    settings_.setValue(QStringLiteral("monitor_override"), monitorOverride_->text());
    settings_.setValue(QStringLiteral("sink"), output_->currentData().toString());
  }

  QSettings settings_;
  QPalette basePalette_;
  QThread workerThread_;
  SenderWorker *worker_ = nullptr;
  bool running_ = false;
  bool exiting_ = false;
  QString selectedSink_;
  QLabel *subtitle_ = nullptr;
  QLabel *connectionTitle_ = nullptr;
  QLabel *modeLabel_ = nullptr;
  QLabel *addressLabel_ = nullptr;
  QLabel *outputLabel_ = nullptr;
  QLabel *connectionHint_ = nullptr;
  QLabel *audioTitle_ = nullptr;
  QLabel *bufferLabel_ = nullptr;
  QLabel *prerollLabel_ = nullptr;
  QLabel *gateLabel_ = nullptr;
  QLabel *outputDbLabel_ = nullptr;
  QLabel *monitorLabel_ = nullptr;
  QLabel *logsTitle_ = nullptr;
  QLabel *status_ = nullptr;
  QLabel *stats_ = nullptr;
  QComboBox *language_ = nullptr;
  QComboBox *theme_ = nullptr;
  QComboBox *mode_ = nullptr;
  QComboBox *output_ = nullptr;
  QLineEdit *address_ = nullptr;
  QLineEdit *monitorOverride_ = nullptr;
  QSpinBox *port_ = nullptr;
  QSpinBox *buffer_ = nullptr;
  QSpinBox *preroll_ = nullptr;
  QSpinBox *gate_ = nullptr;
  QSpinBox *outputDb_ = nullptr;
  QCheckBox *keepalive_ = nullptr;
  QCheckBox *adaptive_ = nullptr;
  QPushButton *refresh_ = nullptr;
  QPushButton *start_ = nullptr;
  QPushButton *quit_ = nullptr;
  QToolButton *developers_ = nullptr;
  QToolButton *bufferHelp_ = nullptr;
  QToolButton *prerollHelp_ = nullptr;
  QToolButton *gateHelp_ = nullptr;
  QToolButton *outputDbHelp_ = nullptr;
  QToolButton *monitorHelp_ = nullptr;
  QFrame *developerCard_ = nullptr;
  QPlainTextEdit *logs_ = nullptr;
  QSystemTrayIcon *tray_ = nullptr;
  QMenu *trayMenu_ = nullptr;
  QAction *trayOpen_ = nullptr;
  QAction *trayQuit_ = nullptr;
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("AshaOS Linux"));
  app.setOrganizationName(QStringLiteral("AshaOS"));
  app.setStyle(QStringLiteral("Fusion"));
  MainWindow window;
  window.show();
  return app.exec();
}
