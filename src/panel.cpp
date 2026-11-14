#include "panel.h"
#include "filelink.h"
#include <KLocalizedString>
#include <KTextEditor/Document>
#include <KTextEditor/MainWindow>
#include <KTextEditor/View>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGridLayout>
#include <QResizeEvent>
#include <QShowEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QSplitter>
#include <QSettings>
#include <QSlider>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>

namespace {
class OfflineRequests : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        info.block(info.requestUrl().scheme() != QLatin1String("qrc"));
    }
};
class TranscriptPage : public QWebEnginePage {
public:
    TranscriptPage(QWebEngineProfile *profile, VoicePanel *panel) : QWebEnginePage(profile, panel), panel(panel) {}
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool mainFrame) override {
        if (type == NavigationTypeLinkClicked) { panel->openLink(url.toString()); return false; }
        return !mainFrame || url == QUrl(QStringLiteral("qrc:/voicepanel/web/index.html"));
    }
    QWebEnginePage *createWindow(WebWindowType) override { return nullptr; }
private:
    VoicePanel *panel;
};
QIcon icon(const QString &name, QStyle::StandardPixmap fallback) {
    return QIcon::fromTheme(name, QApplication::style()->standardIcon(fallback));
}
QString device(QComboBox *box) {
    const auto value = box->currentData().toString();
    return box->currentIndex() >= 0 ? value : box->currentText().trimmed();
}
QSettings preferences() { return QSettings(QStringLiteral("kate-voice-panel"), QStringLiteral("settings")); }
void stopProcess(QProcess &process) {
    if (process.state() == QProcess::NotRunning) return;
    process.terminate();
    if (!process.waitForFinished(900)) { process.kill(); process.waitForFinished(300); }
}
}

VoicePanel::VoicePanel(KTextEditor::MainWindow *window, QWidget *parent)
    : QWidget(parent), m_window(window)
{
    setObjectName(QStringLiteral("kateVoicePanel"));
    // Isolate RHI initialization from Kate while keeping all tool controls on one surface.
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_NativeWindow);
    auto outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4); outer->setSpacing(0); outer->setSizeConstraint(QLayout::SetNoConstraint);
    m_split = new QSplitter(Qt::Horizontal, this); m_split->setObjectName(QStringLiteral("voiceSplit"));
    m_split->setChildrenCollapsible(false); outer->addWidget(m_split);
    m_controls = new QWidget(m_split);
    auto controlsLayout = new QHBoxLayout(m_controls); controlsLayout->setContentsMargins(0, 0, 4, 0); controlsLayout->setAlignment(Qt::AlignTop);
    m_autoInput = new QToolButton(this); m_autoInput->setObjectName(QStringLiteral("voiceEnabled"));
    m_autoInput->setCheckable(true); m_autoInput->setText(i18n("Voice input\nOff"));
    m_autoInput->setIcon(icon(QStringLiteral("audio-input-microphone"), QStyle::SP_MediaVolume));
    m_autoInput->setIconSize(QSize(40, 40)); m_autoInput->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_autoInput->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_autoInput->setToolTip(i18n("Enable voice input. Speak above the threshold, then pause to send. Turn off to stop."));
    controlsLayout->addWidget(m_autoInput);
    auto right = new QWidget(m_split); auto rightLayout = new QVBoxLayout(right); rightLayout->setContentsMargins(0, 0, 0, 0); rightLayout->setSpacing(0);
    auto toolbar = new QHBoxLayout;
    m_scope = new QLabel(this); m_scope->setObjectName(QStringLiteral("voiceScope"));
    m_scope->setTextInteractionFlags(Qt::TextSelectableByMouse); m_scope->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    toolbar->addWidget(m_scope, 1);
    m_new = new QToolButton(this); m_new->setText(i18n("New conversation")); m_new->setToolTip(m_new->text());
    m_new->setObjectName(QStringLiteral("voiceNew")); m_new->setIcon(icon(QStringLiteral("document-new"), QStyle::SP_FileIcon));
    toolbar->addWidget(m_new); rightLayout->addLayout(toolbar);

    auto saved = preferences();
    m_settings = new QWidget(this); m_settings->setObjectName(QStringLiteral("voiceSettings"));
    auto grid = new QGridLayout(m_settings); grid->setContentsMargins(0, 0, 0, 0); grid->setSpacing(4);
    grid->setColumnStretch(0, 1); grid->setColumnStretch(1, 1);
    const auto cell = [this, grid](int row, int column, const QString &title, QWidget *control) {
        auto group = new QWidget(m_settings); auto box = new QVBoxLayout(group); box->setContentsMargins(0, 0, 0, 0); box->setSpacing(2);
        auto label = new QLabel(title, group); label->setBuddy(control); label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        box->addWidget(label); box->addWidget(control);
        grid->addWidget(group, row, column);
    };
    m_threshold = new QSpinBox(this); m_threshold->setObjectName(QStringLiteral("voiceThreshold"));
    m_threshold->setRange(-60, -10); m_threshold->setSuffix(QStringLiteral(" dBFS"));
    m_threshold->setValue(saved.value(QStringLiteral("threshold"), -35).toInt());
    m_threshold->setToolTip(i18n("Lower values detect quieter speech. Brief sounds shorter than 200 ms are ignored."));
    m_silence = new QSpinBox(this); m_silence->setObjectName(QStringLiteral("voiceSilence"));
    m_silence->setRange(400, 2000); m_silence->setSingleStep(100); m_silence->setSuffix(QStringLiteral(" ms"));
    m_silence->setValue(saved.value(QStringLiteral("silence"), 900).toInt());
    m_silence->setToolTip(i18n("Send after this much silence"));
    m_threshold->setAccessibleName(i18n("Speech threshold")); m_silence->setAccessibleName(i18n("Silence before sending"));

    m_level = new QProgressBar(this); m_level->setRange(-60, 0); m_level->setValue(-60); m_level->setTextVisible(false);
    m_level->setAccessibleName(i18n("Microphone level"));
    m_voice = new QComboBox(this); m_voice->setObjectName(QStringLiteral("voiceVoice"));
    m_voice->addItem(i18n("Stella"), QStringLiteral("stella")); m_voice->addItem(i18n("Tide"), QStringLiteral("tide"));
    m_voice->setCurrentIndex(qMax(0, m_voice->findData(saved.value(QStringLiteral("voice"), QStringLiteral("stella")))));
    m_microphone = new QComboBox(this); m_speaker = new QComboBox(this);
    m_microphone->setObjectName(QStringLiteral("voiceMicrophone")); m_speaker->setObjectName(QStringLiteral("voiceSpeaker"));
    for (auto box : {m_microphone, m_speaker}) {
        box->setEditable(true); box->setCompleter(nullptr); box->setMinimumContentsLength(12);
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); box->addItem(i18n("System default"), QString());
        const auto name = saved.value(box == m_microphone ? QStringLiteral("microphone") : QStringLiteral("speaker")).toString();
        if (!name.isEmpty()) { box->addItem(name, name); box->setCurrentIndex(1); }
    }
    const auto inputChanged = [this] {
        saveSettings();
        if (m_autoInput->isChecked() && !m_busy) { pauseListening(); QTimer::singleShot(0, this, &VoicePanel::startListening); }
    };
    connect(m_microphone, &QComboBox::activated, this, inputChanged);
    connect(m_microphone->lineEdit(), &QLineEdit::editingFinished, this, inputChanged);
    m_volume = new QSlider(Qt::Horizontal, this); m_volume->setRange(0, 100);
    m_volume->setValue(saved.value(QStringLiteral("volume"), 80).toInt()); m_volume->setAccessibleName(i18n("Playback volume"));
    m_readAloud = new QCheckBox(i18n("Enabled"), this); m_readAloud->setAccessibleName(i18n("Read replies aloud"));
    m_readAloud->setChecked(saved.value(QStringLiteral("speak"), true).toBool());
    cell(0, 0, i18n("Microphone"), m_microphone); cell(0, 1, i18n("Output"), m_speaker);
    cell(1, 0, i18n("Threshold"), m_threshold); cell(1, 1, i18n("Silence"), m_silence);
    cell(2, 0, i18n("Voice"), m_voice); cell(2, 1, i18n("Volume"), m_volume);
    cell(3, 0, i18n("Input level"), m_level); cell(3, 1, i18n("Read replies"), m_readAloud);
    controlsLayout->addWidget(m_settings, 1);
    connect(m_new, &QToolButton::clicked, this, &VoicePanel::reset);

    m_transcript = new QWebEngineView(this); m_transcript->setObjectName(QStringLiteral("voiceTranscript"));
    auto profile = new QWebEngineProfile(this);
    profile->setUrlRequestInterceptor(new OfflineRequests(profile));
    auto page = new TranscriptPage(profile, this); page->setParent(m_transcript); m_transcript->setPage(page);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    page->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    auto channel = new QWebChannel(page); channel->registerObject(QStringLiteral("panel"), new VoiceTranscriptBridge(this));
    page->setWebChannel(channel); rightLayout->addWidget(m_transcript, 1);
    m_split->setStretchFactor(0, 0); m_split->setStretchFactor(1, 1);
    m_split->setSizes({m_controls->sizeHint().width(), 700});
    connect(m_split, &QSplitter::splitterMoved, this, [this] { scheduleSpaceCheck(); });
    setFocusProxy(m_autoInput);
    m_activity.setThreshold(m_threshold->value()); m_activity.setSilence(m_silence->value());
    connect(m_threshold, &QSpinBox::valueChanged, &m_activity, &VoiceActivity::setThreshold);
    connect(m_silence, &QSpinBox::valueChanged, &m_activity, &VoiceActivity::setSilence);
    connect(m_autoInput, &QToolButton::toggled, this, [this](bool enabled) {
        if (enabled) startListening();
        else { stop(); }
        saveSettings(); updateControls();
    });
    connect(&m_listener, &QProcess::readyReadStandardOutput, this, [this] {
        const auto audio = m_listener.readAllStandardOutput();
        if (m_autoInput->isChecked() && !m_busy) m_activity.feed(audio);
    });
    connect(&m_listener, &QProcess::readyReadStandardError, this, [this] { m_listener.readAllStandardError(); });
    connect(&m_listener, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_closing && error == QProcess::FailedToStart) {
            m_autoInput->setChecked(false); receive({{"type", "error"}, {"text", i18n("Cannot start the microphone. Install pw-record and check PipeWire permissions.")}});
        }
    });
    connect(&m_listener, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        if (m_closing || !m_autoInput->isChecked() || m_busy) return;
        if (code != 0 && exit == QProcess::NormalExit) {
            m_autoInput->setChecked(false); receive({{"type", "error"}, {"text", i18n("Microphone input failed. Check the selected device.")}});
        } else QTimer::singleShot(0, this, &VoicePanel::startListening);
    });
    connect(&m_activity, &VoiceActivity::levelChanged, m_level, &QProgressBar::setValue);
    connect(&m_activity, &VoiceActivity::speechStarted, this, [this] {
        m_recordContext = editorContext(); m_recordWorkspace = workspace();
        publish({{"type", "status"}, {"state", "hearing"}});
    });
    connect(&m_activity, &VoiceActivity::speechDiscarded, this, [this] { publish({{"type", "status"}, {"state", "listening"}}); });
    connect(&m_activity, &VoiceActivity::utterance, this, [this](const QByteArray &pcm) {
        pauseListening();
        QFile file(m_directory.filePath(QStringLiteral("recording.wav")));
        if (!file.open(QIODevice::WriteOnly) || file.write(VoiceActivity::wav(pcm)) != pcm.size() + 44) {
            m_autoInput->setChecked(false); receive({{"type", "error"}, {"text", i18n("Cannot save the recording.")}}); return;
        }
        file.close(); ask({}, file.fileName());
    });
    connect(&m_bridge, &QProcess::readyReadStandardOutput, this, [this] {
        m_buffer += m_bridge.readAllStandardOutput();
        if (m_buffer.size() > 4 * 1024 * 1024) { m_bridge.kill(); receive({{"type", "error"}, {"text", i18n("Voice bridge output exceeded the size limit.")}}); return; }
        while (m_buffer.contains('\n')) {
            const auto end = m_buffer.indexOf('\n'); const auto line = m_buffer.left(end); m_buffer.remove(0, end + 1);
            const auto item = QJsonDocument::fromJson(line);
            if (item.isObject()) receive(item.object());
        }
    });
    // Raw stderr can contain provider details; the bridge reports sanitized errors over JSON.
    connect(&m_bridge, &QProcess::readyReadStandardError, this, [this] { m_bridge.readAllStandardError(); });
    connect(&m_bridge, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_closing && error == QProcess::FailedToStart) {
            m_commands.clear(); m_busy = false; updateControls();
            receive({{"type", "error"}, {"text", i18n("Cannot start Node.js. Install Node.js 24.5 or newer.")}});
            publish({{"type", "status"}, {"state", "idle"}});
        }
    });
    connect(&m_bridge, &QProcess::finished, this, [this] {
        m_bridgeReady = false; m_commands.clear(); m_busy = false; updateControls();
        if (!m_closing) receive({{"type", "error"}, {"text", i18n("Voice bridge stopped. Check Node.js 24.5+ and DSH, then send again.")}});
        publish({{"type", "status"}, {"state", "idle"}});
    });
    connect(&m_playback, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_closing && error == QProcess::FailedToStart) receive({{"type", "error"}, {"text", i18n("Cannot start pw-play. The reply is available in the transcript.")}});
        updateControls();
    });
    connect(&m_playback, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (m_closing) return;
        if (code != 0 && status == QProcess::NormalExit) receive({{"type", "error"}, {"text", i18n("Audio playback failed. Check the output device.")}});
        if (!m_busy) { publish({{"type", "status"}, {"state", "idle"}}); QTimer::singleShot(250, this, &VoicePanel::startListening); } updateControls();
    });
    connect(&m_devices, &QProcess::finished, this, [this](int code) {
        if (code != 0) return;
        const auto nodes = QJsonDocument::fromJson(m_devices.readAllStandardOutput()).array();
        const auto mic = device(m_microphone), speaker = device(m_speaker);
        for (auto box : {m_microphone, m_speaker}) { box->clear(); box->addItem(i18n("System default"), QString()); }
        for (const auto &node : nodes) {
            const auto props = node.toObject().value("info").toObject().value("props").toObject();
            const auto kind = props.value("media.class").toString();
            auto box = kind == QLatin1String("Audio/Source") ? m_microphone : kind == QLatin1String("Audio/Sink") ? m_speaker : nullptr;
            if (box) box->addItem(props.value("node.description").toString(props.value("node.name").toString()), props.value("node.name").toString());
        }
        for (auto box : {m_microphone, m_speaker}) {
            const auto selected = box == m_microphone ? mic : speaker;
            if (box->findData(selected) < 0) box->addItem(selected, selected);
            box->setCurrentIndex(box->findData(selected));
        }
    });
    updateControls(); updateTheme();
    QTimer::singleShot(0, this, &VoicePanel::refreshDevices);
    m_transcript->load(QUrl(QStringLiteral("qrc:/voicepanel/web/index.html")));
}

VoicePanel::~VoicePanel()
{
    m_closing = true; saveSettings();
    for (auto process : {&m_playback, &m_devices, &m_listener}) stopProcess(*process);
    m_bridge.closeWriteChannel();
    if (m_bridge.state() != QProcess::NotRunning && !m_bridge.waitForFinished(1200)) stopProcess(m_bridge);
    delete m_transcript; m_transcript = nullptr;
}

QString VoicePanel::workspace() const
{
    if (auto project = m_window->pluginView(QStringLiteral("kateprojectplugin"))) {
        const auto path = project->property("projectBaseDir").toString();
        if (QDir::isAbsolutePath(path) && QFileInfo(path).isDir()) return QFileInfo(path).canonicalFilePath();
    }
    if (auto view = m_window->activeView(); view && view->document()->url().isLocalFile())
        return QFileInfo(view->document()->url().toLocalFile()).absolutePath();
    return QDir::homePath();
}

QJsonObject VoicePanel::editorContext() const
{
    QJsonObject context{{"workspace", workspace()}};
    QJsonArray files;
    QSet<QString> seen;
    for (auto view : m_window->views()) {
        const auto document = view->document(); const auto path = document->url().toDisplayString(QUrl::PreferLocalFile);
        if (!seen.contains(path) && files.size() < 100) { files.append(QJsonObject{{"path", path}, {"modified", document->isModified()}}); seen.insert(path); }
    }
    context.insert("openFiles", files);
    if (auto view = m_window->activeView()) {
        const auto document = view->document(); const auto cursor = view->cursorPosition();
        const int first = qMax(0, cursor.line() - 80), last = qMin(document->lines() - 1, cursor.line() + 80);
        const auto excerpt = document->text(KTextEditor::Range(first, 0, last, document->lineLength(last)));
        const auto selection = view->selectionText();
        context.insert("activeFile", QJsonObject{{"path", document->url().toDisplayString(QUrl::PreferLocalFile)},
            {"modified", document->isModified()}, {"cursorLine", cursor.line() + 1}, {"cursorColumn", cursor.column() + 1},
            {"selection", selection.left(48000)}, {"selectionTruncated", selection.size() > 48000},
            {"selectionStartLine", view->selection() ? view->selectionRange().start().line() + 1 : 0},
            {"excerptStartLine", first + 1}, {"excerpt", excerpt.left(64000)},
            {"excerptTruncated", first > 0 || last < document->lines() - 1 || excerpt.size() > 64000}});
    }
    return context;
}

void VoicePanel::saveSettings()
{
    auto saved = preferences();
    saved.setValue("threshold", m_threshold->value()); saved.setValue("silence", m_silence->value());
    saved.setValue("voice", m_voice->currentData()); saved.setValue("microphone", device(m_microphone));
    saved.setValue("speaker", device(m_speaker)); saved.setValue("volume", m_volume->value()); saved.setValue("speak", m_readAloud->isChecked());
}

void VoicePanel::refreshDevices()
{
    if (m_devices.state() != QProcess::NotRunning) return;
    m_devices.start(QStringLiteral("pw-dump"), {});
    QTimer::singleShot(3000, &m_devices, [this] { if (m_devices.state() != QProcess::NotRunning) m_devices.kill(); });
}

void VoicePanel::startBridge()
{
    if (m_bridge.state() != QProcess::NotRunning) return;
    const auto failed = [this](const QString &message) {
        m_commands.clear(); m_busy = false; m_autoInput->setChecked(false); updateControls();
        publish({{"type", "error"}, {"text", message}}); publish({{"type", "status"}, {"state", "idle"}});
    };
    if (!m_directory.isValid()) { failed(i18n("Cannot create a private voice directory.")); return; }
    for (const auto name : {"main.mjs", "deepseek.mjs", "dsh.mjs", "profile.mjs", "readonly.mjs", "tools.mjs"}) {
        QFile source(QStringLiteral(":/voicepanel/bridge/") + QLatin1String(name)); QFile target(m_directory.filePath(QLatin1String(name)));
        if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0) {
            failed(i18n("Cannot prepare the voice bridge.")); return;
        }
    }
    auto node = qEnvironmentVariable("KATE_VOICE_NODE");
    if (node.isEmpty()) node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty()) { failed(i18n("Node.js 24.5 or newer is required.")); return; }
    m_buffer.clear(); m_bridgeReady = false;
    m_bridge.start(node, {m_directory.filePath(QStringLiteral("main.mjs"))});
}

void VoicePanel::send(const QJsonObject &request)
{
    Q_EMIT requestSent(request);
    if (m_bridgeReady) m_bridge.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    else { m_commands.append(request); startBridge(); }
}

void VoicePanel::ask(const QString &text, const QString &audio)
{
    saveSettings();
    pauseListening();
    if (m_playback.state() != QProcess::NotRunning) m_playback.kill();
    const auto cwd = audio.isEmpty() ? workspace() : m_recordWorkspace;
    const auto context = audio.isEmpty() ? editorContext() : m_recordContext;
    if (!m_sessionWorkspace.isEmpty() && m_sessionWorkspace != cwd)
        publish({{"type", "notice"}, {"text", i18n("Workspace changed. Starting a new conversation.")}});
    m_sessionWorkspace = cwd; m_busy = true; updateControls();
    m_request = QUuid::createUuid().toString(QUuid::WithoutBraces);
    publish({{"type", "status"}, {"state", "starting"}});
    send({{"type", "ask"}, {"request", m_request}, {"text", text}, {"audio", audio}, {"cwd", cwd}, {"context", context},
        {"voice", m_voice->currentData().toString()},
        {"speak", m_readAloud->isChecked()}});
}

void VoicePanel::stop()
{
    { const QSignalBlocker blocker(m_autoInput); m_autoInput->setChecked(false); }
    pauseListening();
    const bool cancelTurn = m_busy;
    if (m_playback.state() != QProcess::NotRunning) m_playback.kill();
    m_commands.clear();
    m_request = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (cancelTurn && m_bridge.state() != QProcess::NotRunning) send({{"type", "stop"}, {"request", m_request}});
    m_busy = false; updateControls(); publish({{"type", "status"}, {"state", "idle"}, {"text", i18n("Stopped")}});
}

void VoicePanel::reset()
{
    const bool listening = m_autoInput->isChecked();
    stop(); m_sessionWorkspace.clear(); m_events.clear();
    if (m_bridge.state() != QProcess::NotRunning) send({{"type", "reset"}, {"request", m_request}});
    publish({{"type", "clear"}}); updateControls();
    if (listening) m_autoInput->setChecked(true);
}

void VoicePanel::play(const QString &path)
{
    if (QFileInfo(path).canonicalFilePath() != QFileInfo(m_directory.filePath(QStringLiteral("reply.wav"))).canonicalFilePath()) return;
    QStringList args{QStringLiteral("--volume"), QString::number(m_volume->value() / 100.0, 'f', 2)};
    if (!device(m_speaker).isEmpty()) args << QStringLiteral("--target") << device(m_speaker);
    args << path; m_playback.start(QStringLiteral("pw-play"), args);
    updateControls(); publish({{"type", "status"}, {"state", "speaking"}});
}

void VoicePanel::receive(const QJsonObject &event)
{
    if (event.contains("request") && event.value("request").toString() != m_request) return;
    const auto type = event.value("type").toString();
    if (type == QLatin1String("ready")) {
        m_bridgeReady = true; const auto commands = std::exchange(m_commands, {});
        for (const auto &command : commands) m_bridge.write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
        return;
    }
    if (type == QLatin1String("audio")) { play(event.value("path").toString()); return; }
    if (type == QLatin1String("status")) {
        m_busy = event.value("state").toString() != QLatin1String("idle"); updateControls();
        if (!m_busy && m_playback.state() != QProcess::NotRunning) return;
        if (!m_busy) QTimer::singleShot(250, this, &VoicePanel::startListening);
    }
    publish(event);
}

void VoicePanel::publish(const QJsonObject &event)
{
    if (!m_ready) { if (m_events.size() < 1000) m_events.append(event); return; }
    Q_EMIT transcriptEvent(QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
}

void VoicePanel::rendererReady()
{
    m_ready = true; updateTheme();
    for (const auto &event : std::exchange(m_events, {})) publish(event);
}

void VoicePanel::updateControls()
{
    const bool playing = m_playback.state() != QProcess::NotRunning;
    m_autoInput->setText(m_autoInput->isChecked() ? i18n("Voice input\nOn") : i18n("Voice input\nOff"));
    m_settings->setEnabled(!m_busy && !playing); m_new->setEnabled(!m_busy);
    const auto path = workspace(); const auto name = QFileInfo(path).fileName();
    m_scope->setText(i18n("Read-only · %1", name.isEmpty() ? path : name)); m_scope->setToolTip(path);
}

void VoicePanel::startListening()
{
    if (m_closing || !m_autoInput->isChecked() || m_busy || m_playback.state() != QProcess::NotRunning
        || m_listener.state() != QProcess::NotRunning) return;
    m_activity.reset();
    QStringList args{QStringLiteral("--raw"), QStringLiteral("--rate"), QStringLiteral("16000"),
        QStringLiteral("--channels"), QStringLiteral("1"), QStringLiteral("--format"), QStringLiteral("s16")};
    if (!device(m_microphone).isEmpty()) args << QStringLiteral("--target") << device(m_microphone);
    args << QStringLiteral("-");
    m_listener.start(QStringLiteral("pw-record"), args);
    publish({{"type", "status"}, {"state", "listening"}});
}

void VoicePanel::pauseListening()
{
    m_activity.reset(); m_level->setValue(-60);
    if (m_listener.state() != QProcess::NotRunning) m_listener.kill();
}

void VoicePanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange) { updateTheme(); scheduleSpaceCheck(); }
}

int VoicePanel::requiredHeight() const
{
    return m_settings->layout()->minimumSize().height() + layout()->contentsMargins().top() + layout()->contentsMargins().bottom();
}

void VoicePanel::scheduleSpaceCheck()
{
    if (m_spaceCheckPending) return;
    m_spaceCheckPending = true;
    QTimer::singleShot(0, this, [this] {
        m_spaceCheckPending = false;
        if (!isVisible()) return;
        const int controlsHeight = m_settings->layout()->minimumSize().height();
        m_settings->setFixedHeight(controlsHeight); m_autoInput->setFixedHeight(controlsHeight);
        const int limit = qMax(1, (m_split->width() - m_split->handleWidth()) * 49 / 100);
        m_controls->setMaximumWidth(limit);
        const auto sizes = m_split->sizes();
        if (!sizes.isEmpty() && sizes.first() > limit)
            m_split->setSizes({limit, qMax(1, m_split->width() - m_split->handleWidth() - limit)});
        if (height() < requiredHeight()) {
            qDebug("Voice Panel: hiding at height %d; controls require %d", height(), requiredHeight());
            Q_EMIT insufficientSpace();
        }
    });
}

void VoicePanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event); scheduleSpaceCheck();
}

void VoicePanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event); scheduleSpaceCheck();
}

void VoicePanel::updateTheme()
{
    if (!m_transcript) return;
    const auto p = palette(); m_transcript->page()->setBackgroundColor(p.color(QPalette::Base));
    if (!m_ready) return;
    publish({{"type", "theme"}, {"background", p.color(QPalette::Base).name()}, {"foreground", p.color(QPalette::Text).name()},
        {"muted", p.color(QPalette::PlaceholderText).name() + QStringLiteral("%1").arg(p.color(QPalette::PlaceholderText).alpha(), 2, 16, QLatin1Char('0'))}, {"accent", p.color(QPalette::Link).name()},
        {"border", p.color(QPalette::Mid).name()}, {"fontFamily", QFontInfo(font()).family()}, {"fontSize", QFontInfo(font()).pixelSize()},
        {"fixedFontFamily", QFontInfo(QFontDatabase::systemFont(QFontDatabase::FixedFont)).family()},
        {"colorScheme", p.color(QPalette::Base).lightness() < 128 ? "dark" : "light"}});
}

void VoicePanel::openLink(const QString &href)
{
    const QUrl url(href);
    if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")) { QDesktopServices::openUrl(url); return; }
    const auto file = resolveFileLink(href, m_sessionWorkspace.isEmpty() ? workspace() : m_sessionWorkspace);
    if (!file.valid) { publish({{"type", "error"}, {"text", i18n("File not found: %1", href.left(150))}}); return; }
    if (auto view = m_window->openUrl(file.url)) {
        view->removeSelection(); const int line = qMin(file.line, qMax(0, view->document()->lines() - 1));
        view->setCursorPosition(KTextEditor::Cursor(line, qMin(file.column, view->document()->lineLength(line))));
        if (file.endLine >= 0) { const int end = qMin(file.endLine, view->document()->lines() - 1);
            view->setSelection(KTextEditor::Range(line, 0, end, view->document()->lineLength(end))); }
    }
}

VoiceTranscriptBridge::VoiceTranscriptBridge(VoicePanel *panel) : QObject(panel), m_panel(panel)
{ connect(panel, &VoicePanel::transcriptEvent, this, &VoiceTranscriptBridge::transcriptEvent); }
void VoiceTranscriptBridge::rendererReady() { m_panel->rendererReady(); }
void VoiceTranscriptBridge::openLink(const QString &href) { m_panel->openLink(href); }
