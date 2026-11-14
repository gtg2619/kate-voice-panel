#include "panel.h"
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/MainWindow>
#include <KTextEditor/View>
#include <KTextEditor/Plugin>
#include <KPluginFactory>
#include <KActionCollection>
#include <KXMLGUIClient>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QGridLayout>
#include <QPluginLoader>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QProgressBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QWindow>
#include <memory>

// Kate's ToolView uses its current size as its preferred size.
class ToolView : public QWidget {
public:
    using QWidget::QWidget;
    QSize sizeHint() const override { return size(); }
    QSize minimumSizeHint() const override { return {160, 160}; }
};

class Host : public QObject
{
    Q_OBJECT
public:
    KTextEditor::View *view = nullptr;
    QList<KTextEditor::View *> allViews;
    QObject *project = nullptr;
    QPointer<QWidget> tool;
    QWidget *parentWidget = nullptr;
    QSplitter *dock = nullptr;
    QUrl opened;
    QIcon toolIcon;
    int hideCount = 0;
public Q_SLOTS:
    KTextEditor::View *activeView() { return view; }
    QList<KTextEditor::View *> views() { return allViews.isEmpty() && view ? QList<KTextEditor::View *>{view} : allViews; }
    QObject *pluginView(const QString &name) { return name == QLatin1String("kateprojectplugin") ? project : nullptr; }
    KTextEditor::View *openUrl(const QUrl &url, const QString &) { opened = url; return view; }
    QWidget *window() { return view ? view->window() : nullptr; }
    QWidget *createViewBar(KTextEditor::View *) { return nullptr; }
    void deleteViewBar(KTextEditor::View *) {}
    bool showToolView(QWidget *widget) { widget->show(); return true; }
    bool hideToolView(QWidget *widget) { ++hideCount; widget->hide(); return true; }
    KXMLGUIFactory *guiFactory() { return nullptr; }
    QWidget *createToolView(KTextEditor::Plugin *, const QString &, KTextEditor::MainWindow::ToolViewPosition, const QIcon &icon, const QString &) {
        toolIcon = icon; tool = new ToolView(parentWidget); auto box = new QVBoxLayout(tool); box->setContentsMargins(0, 0, 0, 0);
        if (dock) dock->addWidget(tool);
        if (parentWidget) parentWidget->layout()->addWidget(tool); tool->hide(); return tool;
    }
};

class PanelTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_temp;
    QByteArray m_path;
    QVariant js(VoicePanel &panel, const QString &script) {
        QVariant result; QEventLoop loop; QPointer<QEventLoop> guard(&loop);
        QTimer::singleShot(4000, &loop, &QEventLoop::quit);
        panel.findChild<QWebEngineView *>()->page()->runJavaScript(QStringLiteral("document.body ? (") + script + QStringLiteral(") : null"), [guard, &result](const QVariant &value) {
            if (guard) { result = value; guard->quit(); }
        }); loop.exec(); return result;
    }
    void show(VoicePanel &panel) { panel.resize(1000, 400); panel.show(); QVERIFY(QTest::qWaitForWindowExposed(&panel)); }
    QJsonObject ask(QSignalSpy &spy) {
        for (const auto &row : spy) { const auto value = row.at(0).toJsonObject(); if (value.value("type") == "ask") return value; }
        return {};
    }
private Q_SLOTS:
    void init() {
        m_temp = std::make_unique<QTemporaryDir>(); m_path = qgetenv("PATH");
        qputenv("KATE_VOICE_NODE", QByteArray(SOURCE_DIR) + "/tests/fake_bridge.py");
        qunsetenv("KATE_VOICE_TEST_SPEECH");
        for (const auto name : {"pw-record", "pw-play", "pw-dump"})
            QVERIFY(QFile::link(QStringLiteral(SOURCE_DIR "/tests/fake_audio.py"), m_temp->filePath(QLatin1String(name))));
        qputenv("PATH", m_temp->path().toUtf8() + ":" + m_path);
        QSettings(QStringLiteral("kate-voice-panel"), QStringLiteral("settings")).clear();
    }
    void cleanup() { qputenv("PATH", m_path); qunsetenv("KATE_VOICE_NODE"); qunsetenv("KATE_VOICE_TEST_SPEECH"); qunsetenv("KATE_VOICE_TEST_PLAYBACK"); }
    void settingsAndSplitLayout() {
        Host host; KTextEditor::MainWindow window(&host); VoicePanel panel(&window); show(panel);
        auto split = panel.findChild<QSplitter *>(QStringLiteral("voiceSplit")); QVERIFY(split); QCOMPARE(split->count(), 2);
        const auto sizes = split->sizes(); const auto ratio = double(sizes[0]) / (sizes[0] + sizes[1]);
        QVERIFY2(ratio > .2 && ratio < .5, qPrintable(QString::number(ratio)));
        QVERIFY(!panel.findChild<QLineEdit *>(QStringLiteral("voiceModel")));
        QVERIFY(!panel.findChild<QLineEdit *>(QStringLiteral("voiceEffort")));
        QVERIFY(!panel.findChild<QToolButton *>(QStringLiteral("voiceEnabled"))->isChecked());
        QVERIFY(panel.findChild<QWidget *>(QStringLiteral("voiceSettings"))->isVisible());
        QVERIFY(panel.findChildren<QScrollArea *>().isEmpty());
        auto grid = qobject_cast<QGridLayout *>(panel.findChild<QWidget *>(QStringLiteral("voiceSettings"))->layout());
        QVERIFY(grid); QCOMPARE(grid->rowCount(), 4); QCOMPARE(grid->columnCount(), 2);
        auto enabled = panel.findChild<QToolButton *>(QStringLiteral("voiceEnabled"));
        auto level = panel.findChild<QProgressBar *>(QStringLiteral("voiceLevel"));
        QVERIFY(enabled->height() >= panel.requiredHeight() - 20);
        QTRY_COMPARE(level->width(), enabled->width());
        QTRY_VERIFY(level->mapTo(&panel, QPoint()).y() >= enabled->mapTo(&panel, QPoint(0, enabled->height())).y());
        QCOMPARE(panel.findChild<QDoubleSpinBox *>(QStringLiteral("voiceSpeed"))->value(), 1.0);
        QTRY_VERIFY(js(panel, QStringLiteral("typeof window.receive === 'function'")).toBool());
        QCOMPARE(js(panel, QStringLiteral("document.querySelectorAll('input, textarea, [contenteditable]').length")).toInt(), 0);
    }
    void workspaceAndEditorContext() {
        Host host; KTextEditor::MainWindow window(&host);
        std::unique_ptr<KTextEditor::Document> doc(KTextEditor::Editor::instance()->createDocument(nullptr));
        std::unique_ptr<KTextEditor::View> view(doc->createView(nullptr, &window)); host.view = view.get();
        doc->setText(QStringLiteral("first\nselected code\nlast")); view->setCursorPosition({1, 3}); view->setSelection({1, 0, 1, 8});
        VoicePanel panel(&window);
        QCOMPARE(panel.workspace(), QDir::homePath());
        QObject project; project.setProperty("projectBaseDir", m_temp->path()); host.project = &project;
        QCOMPARE(panel.workspace(), m_temp->path());
        const auto context = panel.editorContext().value("activeFile").toObject();
        QCOMPARE(context.value("selection").toString(), QStringLiteral("selected"));
        QCOMPARE(context.value("cursorLine").toInt(), 2); QVERIFY(context.value("excerpt").toString().contains("last"));
        QVERIFY(context.value("modified").toBool());
    }
    void largeSelectionIsExplicitlyTruncated() {
        Host host; KTextEditor::MainWindow window(&host);
        std::unique_ptr<KTextEditor::Document> doc(KTextEditor::Editor::instance()->createDocument(nullptr));
        std::unique_ptr<KTextEditor::View> view(doc->createView(nullptr, &window)); host.view = view.get();
        doc->setText(QString(30000, QLatin1Char('x')) + QLatin1Char('\n') + QString(40000, QLatin1Char('y'))); view->setSelection({0, 0, 1, 40000});
        VoicePanel panel(&window); const auto active = panel.editorContext().value("activeFile").toObject();
        QCOMPARE(active.value("selection").toString().size(), 48000); QVERIFY(active.value("selectionTruncated").toBool());
        QVERIFY(active.value("excerptTruncated").toBool());
    }
    void automaticRecordingUsesContextAtSpeechStart() {
        Host host; KTextEditor::MainWindow window(&host); QObject project; project.setProperty("projectBaseDir", m_temp->path()); host.project = &project;
        std::unique_ptr<KTextEditor::Document> doc(KTextEditor::Editor::instance()->createDocument(nullptr));
        std::unique_ptr<KTextEditor::View> view(doc->createView(nullptr, &window)); host.view = view.get();
        doc->setText(QStringLiteral("original")); view->setSelection({0, 0, 0, 8});
        VoicePanel panel(&window); show(panel); QSignalSpy sent(&panel, &VoicePanel::requestSent);
        panel.findChild<QDoubleSpinBox *>(QStringLiteral("voiceSpeed"))->setValue(1.5);
        qputenv("KATE_VOICE_TEST_SPEECH", "1");
        panel.findChild<QToolButton *>(QStringLiteral("voiceEnabled"))->setChecked(true);
        QTRY_VERIFY(js(panel, QStringLiteral("document.body.textContent.includes('Hearing speech')")).toBool());
        doc->setText(QStringLiteral("changed"));
        QTRY_VERIFY(!ask(sent).isEmpty());
        QCOMPARE(ask(sent).value("context").toObject().value("activeFile").toObject().value("selection").toString(), QStringLiteral("original"));
        QVERIFY(!ask(sent).value("audio").toString().isEmpty());
        QCOMPARE(ask(sent).value("speed").toDouble(), 1.5);
        QTRY_VERIFY(js(panel, QStringLiteral("document.body.textContent.includes('The formula')")).toBool());
        QVERIFY(js(panel, QStringLiteral("document.querySelector('.katex') !== null")).toBool());
        QVERIFY(js(panel, QStringLiteral("document.querySelector('.file-link') !== null")).toBool());
        QVERIFY(js(panel, QStringLiteral("!document.querySelector('.process').open")).toBool());
    }
    void cancelDoesNotSendAndNextRecordingStillWorks() {
        Host host; KTextEditor::MainWindow window(&host); VoicePanel panel(&window); show(panel); QSignalSpy sent(&panel, &VoicePanel::requestSent);
        qputenv("KATE_VOICE_TEST_SPEECH", "1"); auto enabled = panel.findChild<QToolButton *>(QStringLiteral("voiceEnabled"));
        enabled->setChecked(true); QTest::qWait(80); panel.stop(); QTest::qWait(80); QVERIFY(ask(sent).isEmpty());
        enabled->setChecked(true); QTRY_VERIFY(!ask(sent).isEmpty()); panel.stop();
    }
    void automaticInputWaitsForThresholdAndDoesNotPersistEnabledState() {
        Host host; KTextEditor::MainWindow window(&host);
        { VoicePanel panel(&window); show(panel); QSignalSpy sent(&panel, &VoicePanel::requestSent);
            auto enabled = panel.findChild<QToolButton *>(QStringLiteral("voiceEnabled")); enabled->setChecked(true);
            QTest::qWait(200); QVERIFY(ask(sent).isEmpty()); enabled->setChecked(false); QTest::qWait(80);
            qputenv("KATE_VOICE_TEST_SPEECH", "1"); enabled->setChecked(true);
            QTRY_VERIFY(!ask(sent).isEmpty()); enabled->setChecked(false);
            QVERIFY(ask(sent).value("text").toString().isEmpty());
            panel.findChild<QSpinBox *>(QStringLiteral("voiceThreshold"))->setValue(-42);
        }
        VoicePanel next(&window); QVERIFY(!next.findChild<QToolButton *>(QStringLiteral("voiceEnabled"))->isChecked());
        QCOMPARE(next.findChild<QSpinBox *>(QStringLiteral("voiceThreshold"))->value(), -42);
    }
    void parametersPersistWithoutConversationAndRestoreAfterClosing() {
        Host host; KTextEditor::MainWindow window(&host);
        {
            VoicePanel panel(&window); show(panel); QSignalSpy sent(&panel, &VoicePanel::requestSent);
            auto microphone = panel.findChild<QComboBox *>(QStringLiteral("voiceMicrophone"));
            QTRY_VERIFY(microphone->findData(QStringLiteral("test.mic")) >= 0);
            microphone->setCurrentIndex(microphone->findData(QStringLiteral("test.mic")));
            // Editing a selected item's text must save the new device ID, not the old item's data.
            microphone->setEditText(QStringLiteral("custom.mic"));
            panel.findChild<QComboBox *>(QStringLiteral("voiceSpeaker"))->setEditText(QStringLiteral("custom.speaker"));
            panel.findChild<QSpinBox *>(QStringLiteral("voiceThreshold"))->setValue(-44);
            panel.findChild<QSpinBox *>(QStringLiteral("voiceSilence"))->setValue(1400);
            panel.findChild<QComboBox *>(QStringLiteral("voiceVoice"))->setCurrentIndex(1);
            panel.findChild<QSlider *>(QStringLiteral("voiceVolume"))->setValue(63);
            panel.findChild<QCheckBox *>(QStringLiteral("voiceReadAloud"))->setChecked(false);
            panel.findChild<QDoubleSpinBox *>(QStringLiteral("voiceSpeed"))->setValue(1.6);
            const auto settingsPath = QSettings(QStringLiteral("kate-voice-panel"), QStringLiteral("settings")).fileName();
            // Read the on-disk INI under a separate QSettings identity, avoiding the shared native cache.
            const auto disk = [&settingsPath](const QString &key) { return QSettings(settingsPath, QSettings::IniFormat).value(key); };
            QTRY_COMPARE(disk(QStringLiteral("speed")).toDouble(), 1.6);
            QCOMPARE(disk(QStringLiteral("threshold")).toInt(), -44);
            QCOMPARE(disk(QStringLiteral("silence")).toInt(), 1400);
            QCOMPARE(disk(QStringLiteral("voice")).toString(), QStringLiteral("tide"));
            QCOMPARE(disk(QStringLiteral("microphone")).toString(), QStringLiteral("custom.mic"));
            QCOMPARE(disk(QStringLiteral("speaker")).toString(), QStringLiteral("custom.speaker"));
            QCOMPARE(disk(QStringLiteral("volume")).toInt(), 63);
            QCOMPARE(disk(QStringLiteral("speak")).toBool(), false);
            QVERIFY(sent.isEmpty());
            // Closing within the debounce interval still flushes the final edit.
            panel.findChild<QDoubleSpinBox *>(QStringLiteral("voiceSpeed"))->setValue(1.7);
        }
        VoicePanel next(&window); show(next);
        auto microphone = next.findChild<QComboBox *>(QStringLiteral("voiceMicrophone"));
        QTRY_VERIFY(microphone->findData(QStringLiteral("test.mic")) >= 0);
        QCOMPARE(microphone->currentData().toString(), QStringLiteral("custom.mic"));
        QCOMPARE(next.findChild<QComboBox *>(QStringLiteral("voiceSpeaker"))->currentData().toString(), QStringLiteral("custom.speaker"));
        QCOMPARE(next.findChild<QSpinBox *>(QStringLiteral("voiceThreshold"))->value(), -44);
        QCOMPARE(next.findChild<QSpinBox *>(QStringLiteral("voiceSilence"))->value(), 1400);
        QCOMPARE(next.findChild<QComboBox *>(QStringLiteral("voiceVoice"))->currentData().toString(), QStringLiteral("tide"));
        QCOMPARE(next.findChild<QSlider *>(QStringLiteral("voiceVolume"))->value(), 63);
        QVERIFY(!next.findChild<QCheckBox *>(QStringLiteral("voiceReadAloud"))->isChecked());
        QCOMPARE(next.findChild<QDoubleSpinBox *>(QStringLiteral("voiceSpeed"))->value(), 1.7);
        QVERIFY(!next.findChild<QToolButton *>(QStringLiteral("voiceEnabled"))->isChecked());
    }
    void olderPanelCannotOverwriteAnotherWindowsSettings() {
        Host host; KTextEditor::MainWindow window(&host);
        {
            VoicePanel older(&window);
            {
                VoicePanel newer(&window);
                newer.findChild<QDoubleSpinBox *>(QStringLiteral("voiceSpeed"))->setValue(1.8);
            }
            older.findChild<QSpinBox *>(QStringLiteral("voiceThreshold"))->setValue(-48);
        }
        VoicePanel next(&window);
        QCOMPARE(next.findChild<QDoubleSpinBox *>(QStringLiteral("voiceSpeed"))->value(), 1.8);
        QCOMPARE(next.findChild<QSpinBox *>(QStringLiteral("voiceThreshold"))->value(), -48);
    }
    void dockCanShrinkAfterGrowingAndHideBelowControls() {
        Host host; QSplitter dock(Qt::Vertical); dock.setChildrenCollapsible(false);
        dock.resize(1000, 900); auto editor = new QWidget(&dock); editor->setMinimumHeight(50); host.dock = &dock;
        KTextEditor::MainWindow window(&host);
        QPluginLoader loader(QStringLiteral(PLUGIN_FILE)); auto factory = qobject_cast<KPluginFactory *>(loader.instance()); QVERIFY(factory);
        auto plugin = factory->create<KTextEditor::Plugin>(&host); auto pluginView = plugin->createView(&window);
        const auto cleanup = qScopeGuard([&] { delete pluginView; delete plugin; });
        dock.show(); QVERIFY(QTest::qWaitForWindowExposed(&dock));
        dynamic_cast<KXMLGUIClient *>(pluginView)->actionCollection()->action(QStringLiteral("voice_panel_show"))->trigger();
        QTest::qWait(50); // Finish the queued initial dock restoration before simulating a user drag.
        auto panel = host.tool->findChild<QWidget *>(QStringLiteral("kateVoicePanel")); QVERIFY(panel);
        const int required = panel->findChild<QWidget *>(QStringLiteral("voiceSettings"))->layout()->minimumSize().height() + 8;
        const int small = required + 50;
        for (int i = 0; i < 3; ++i) {
            dock.setSizes({250, 640}); QTRY_VERIFY(host.tool->height() > 550);
            QCOMPARE(panel->sizeHint().height(), required);
            QVERIFY(host.tool->minimumHeight() < required);
            dock.setSizes({900 - small, small}); QTRY_VERIFY(host.tool->height() < small + 10);
            QVERIFY(host.tool->isVisible());
        }
        panel->findChild<QToolButton *>(QStringLiteral("voiceEnabled"))->setChecked(true);
        dock.setSizes({900 - required + 20, required - 20});
        QTRY_VERIFY(host.hideCount > 0);
        QVERIFY(panel->findChild<QToolButton *>(QStringLiteral("voiceEnabled"))->isChecked());
        dynamic_cast<KXMLGUIClient *>(pluginView)->actionCollection()->action(QStringLiteral("voice_panel_show"))->trigger();
        QTRY_VERIFY(host.tool->isVisible() && panel->height() >= required);
        QTest::qWait(100); QVERIFY(host.tool->isVisible());
    }
    void skipSpeechKeepsConversationAndResumesInput_data() {
        QTest::addColumn<bool>("shortcut");
        QTest::newRow("button") << false;
        QTest::newRow("shortcut-with-hidden-panel") << true;
    }
    void waylandDockDragRestoresInput_data() {
        QTest::addColumn<bool>("hideDuringDrag");
        QTest::newRow("release") << false;
        QTest::newRow("hide-during-drag") << true;
    }
    void waylandDockDragRestoresInput() {
        if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) QSKIP("Wayland input-region regression");
        QFETCH(bool, hideDuringDrag);
        Host host; QSplitter dock(Qt::Vertical); dock.resize(1000, 800);
        new QWidget(&dock); host.dock = &dock;
        KTextEditor::MainWindow window(&host);
        QPluginLoader loader(QStringLiteral(PLUGIN_FILE)); auto factory = qobject_cast<KPluginFactory *>(loader.instance()); QVERIFY(factory);
        auto plugin = factory->create<KTextEditor::Plugin>(&host); auto pluginView = plugin->createView(&window);
        const auto cleanup = qScopeGuard([&] { delete pluginView; delete plugin; });
        dock.show(); QVERIFY(QTest::qWaitForWindowExposed(&dock));
        auto showAction = dynamic_cast<KXMLGUIClient *>(pluginView)->actionCollection()->action(QStringLiteral("voice_panel_show"));
        showAction->trigger(); dock.setSizes({400, 400});
        auto panel = host.tool->findChild<QWidget *>(QStringLiteral("kateVoicePanel")); QVERIFY(panel);
        auto surface = panel->windowHandle(); QVERIFY(surface);
        QTest::mousePress(dock.handle(1), Qt::LeftButton);
        QVERIFY(surface->flags().testFlag(Qt::WindowTransparentForInput));
        if (hideDuringDrag) host.hideToolView(host.tool);
        else QTest::mouseRelease(dock.handle(1), Qt::LeftButton);
        QVERIFY(!surface->flags().testFlag(Qt::WindowTransparentForInput));
        if (hideDuringDrag) { QTest::mouseRelease(dock.handle(1), Qt::LeftButton); showAction->trigger(); }
        auto enabled = panel->findChild<QToolButton *>(QStringLiteral("voiceEnabled"));
        QTest::mouseClick(enabled, Qt::LeftButton); QVERIFY(enabled->isChecked());
    }
    void skipSpeechKeepsConversationAndResumesInput() {
        QFETCH(bool, shortcut);
        Host host; QWidget container; container.setLayout(new QVBoxLayout); container.resize(1000, 600); host.parentWidget = &container;
        auto editor = new QLineEdit(&container); container.layout()->addWidget(editor);
        KTextEditor::MainWindow window(&host);
        QPluginLoader loader(QStringLiteral(PLUGIN_FILE)); auto factory = qobject_cast<KPluginFactory *>(loader.instance()); QVERIFY(factory);
        auto plugin = factory->create<KTextEditor::Plugin>(&host); auto pluginView = plugin->createView(&window);
        const auto cleanup = qScopeGuard([&] { delete pluginView; delete plugin; });
        auto actions = dynamic_cast<KXMLGUIClient *>(pluginView)->actionCollection();
        container.show(); QVERIFY(QTest::qWaitForWindowExposed(&container));
        actions->action(QStringLiteral("voice_panel_show"))->trigger();
        auto panel = host.tool->findChild<QWidget *>(QStringLiteral("kateVoicePanel")); QVERIFY(panel);
        auto skip = panel->findChild<QToolButton *>(QStringLiteral("voiceSkipSpeech")); QVERIFY(skip);
        auto action = actions->action(QStringLiteral("voice_panel_skip_speech")); QVERIFY(action); QVERIFY(!action->isEnabled());
        QSignalSpy sent(panel, SIGNAL(requestSent(QJsonObject))); QVERIFY(sent.isValid());
        qputenv("KATE_VOICE_TEST_PLAYBACK", "1"); qputenv("KATE_VOICE_TEST_SPEECH", "1");
        auto enabled = panel->findChild<QToolButton *>(QStringLiteral("voiceEnabled")); enabled->setChecked(true);
        QTRY_VERIFY(action->isEnabled()); QVERIFY(skip->isVisible());
        qunsetenv("KATE_VOICE_TEST_SPEECH");
        QSignalSpy events(panel, SIGNAL(transcriptEvent(QString))); QVERIFY(events.isValid());
        if (shortcut) {
            host.hideToolView(host.tool); container.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&container));
            editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
            QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
        } else QTest::mouseClick(skip, Qt::LeftButton);
        QTRY_VERIFY(!action->isEnabled()); QVERIFY(skip->isHidden()); QVERIFY(enabled->isChecked());
        const auto listening = [&events] {
            for (const auto &row : events) {
                const auto event = QJsonDocument::fromJson(row.at(0).toString().toUtf8()).object();
                if (event.value("state") == QLatin1String("listening")) return true;
            }
            return false;
        };
        QTRY_VERIFY(listening());
        // No stop/reset command: skipping playback must keep the model's conversation.
        QCOMPARE(sent.size(), 1); QCOMPARE(sent.first().at(0).toJsonObject().value("type").toString(), QStringLiteral("ask"));
        // Another utterance is accepted after skipping, even with the panel hidden.
        qputenv("KATE_VOICE_TEST_SPEECH", "1");
        enabled->setChecked(false); enabled->setChecked(true);
        QTRY_COMPARE(sent.size(), 2);
    }
    void staleEventsAndUnsafeLinksAreIgnored() {
        Host host; KTextEditor::MainWindow window(&host); VoicePanel panel(&window); show(panel);
        panel.receive({{"type", "message"}, {"request", "stale"}, {"role", "assistant"}, {"text", "STALE"}});
        panel.openLink(QStringLiteral("javascript:alert(1)")); QVERIFY(host.opened.isEmpty());
        QTRY_VERIFY(js(panel, QStringLiteral("typeof window.receive === 'function'")).toBool());
        QVERIFY(!js(panel, QStringLiteral("document.body.textContent.includes('STALE')")).toBool());
    }
    void themeDoesNotChangeApplicationPalette() {
        const auto original = qApp->palette(); Host host; KTextEditor::MainWindow window(&host); VoicePanel panel(&window); show(panel);
        auto dark = panel.palette(); dark.setColor(QPalette::Base, QColor("#222222")); dark.setColor(QPalette::Text, QColor("#eeeeee")); panel.setPalette(dark);
        QTRY_COMPARE(js(panel, QStringLiteral("getComputedStyle(document.body).backgroundColor")).toString(), QStringLiteral("rgb(34, 34, 34)"));
        QCOMPARE(qApp->palette(), original);
    }
    void insufficientHeightRequestsHideWithoutDisablingVoice() {
        Host host; KTextEditor::MainWindow window(&host); VoicePanel panel(&window); show(panel);
        auto enabled = panel.findChild<QToolButton *>(QStringLiteral("voiceEnabled")); enabled->setChecked(true);
        QSignalSpy hide(&panel, &VoicePanel::insufficientSpace);
        panel.resize(1000, panel.requiredHeight() - 1); QTRY_VERIFY(!hide.isEmpty()); QVERIFY(enabled->isChecked());
        panel.hide(); QTest::qWait(100); QVERIFY(enabled->isChecked());
    }
    void hiddenPanelStillAcceptsSpeech() {
        Host host; KTextEditor::MainWindow window(&host); VoicePanel panel(&window); show(panel);
        qputenv("KATE_VOICE_TEST_SPEECH", "1"); QSignalSpy sent(&panel, &VoicePanel::requestSent);
        auto enabled = panel.findChild<QToolButton *>(QStringLiteral("voiceEnabled")); enabled->setChecked(true); panel.hide();
        QTRY_VERIFY(!ask(sent).isEmpty()); QVERIFY(enabled->isChecked()); panel.stop();
    }
    void newConversationPreservesEnabledInputAndWidthStaysBelowHalf() {
        Host host; KTextEditor::MainWindow window(&host); VoicePanel panel(&window); show(panel);
        auto enabled = panel.findChild<QToolButton *>(QStringLiteral("voiceEnabled")); enabled->setChecked(true);
        panel.reset(); QVERIFY(enabled->isChecked());
        auto split = panel.findChild<QSplitter *>(QStringLiteral("voiceSplit"));
        auto handle = split->handle(1);
        QTest::mousePress(handle, Qt::LeftButton);
        QTest::mouseMove(handle, QPoint(800 - handle->x(), handle->height() / 2));
        QTest::mouseRelease(handle, Qt::LeftButton);
        QTRY_VERIFY(split->sizes().first() < split->width() / 2);
        panel.resize(800, 300); QTRY_VERIFY(split->sizes().first() < split->width() / 2);
        panel.stop();
    }
    void missingRecorderReturnsToIdle() {
        Host host; KTextEditor::MainWindow window(&host); VoicePanel panel(&window); show(panel);
        qputenv("PATH", "/nonexistent"); auto enabled = panel.findChild<QToolButton *>(QStringLiteral("voiceEnabled")); enabled->setChecked(true);
        QTRY_VERIFY(!enabled->isChecked());
        QTRY_VERIFY(js(panel, QStringLiteral("document.body.textContent.includes('Cannot start the microphone')")).toBool());
    }
    void lazyPluginHasIconAndEscapeKeepsInput_data() {
        QTest::addColumn<QString>("focus");
        QTest::newRow("switch") << QStringLiteral("voiceEnabled");
        QTest::newRow("threshold") << QStringLiteral("voiceThreshold");
        QTest::newRow("transcript") << QStringLiteral("voiceTranscript");
    }
    void lazyPluginHasIconAndEscapeKeepsInput() {
        QFETCH(QString, focus);
        Host host; QWidget container; container.setLayout(new QVBoxLayout); container.resize(1000, 500); host.parentWidget = &container;
        KTextEditor::MainWindow window(&host);
        QPluginLoader loader(QStringLiteral(PLUGIN_FILE)); auto factory = qobject_cast<KPluginFactory *>(loader.instance()); QVERIFY(factory);
        auto plugin = factory->create<KTextEditor::Plugin>(&host); QVERIFY(plugin); auto pluginView = plugin->createView(&window);
        const auto cleanup = qScopeGuard([&] { delete pluginView; delete plugin; });
        QVERIFY(!host.tool->findChild<QWebEngineView *>()); QVERIFY(!host.toolIcon.isNull());
        auto client = dynamic_cast<KXMLGUIClient *>(pluginView); QVERIFY(client);
        container.show(); QVERIFY(QTest::qWaitForWindowExposed(&container));
        client->actionCollection()->action(QStringLiteral("voice_panel_show"))->trigger();
        QTRY_VERIFY(host.tool->findChild<QWidget *>(QStringLiteral("kateVoicePanel"))); auto panel = host.tool->findChild<QWidget *>(QStringLiteral("kateVoicePanel"));
        QCOMPARE(host.tool->layout()->count(), 1);
        auto enabled = panel->findChild<QToolButton *>(QStringLiteral("voiceEnabled")); enabled->setChecked(true);
        container.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&container));
        auto target = panel->findChild<QWidget *>(focus); QVERIFY(target); target->setFocus(); QTRY_VERIFY(target->hasFocus());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape); QTRY_COMPARE(host.hideCount, 1); QVERIFY(enabled->isChecked());
    }
    void screenshot() {
        if (qEnvironmentVariableIsEmpty("KATE_VOICE_SCREENSHOT")) QSKIP("Optional screenshot capture");
        Host host; QObject project; QDir().mkpath(m_temp->filePath(QStringLiteral("example")));
        project.setProperty("projectBaseDir", m_temp->filePath(QStringLiteral("example"))); host.project = &project;
        KTextEditor::MainWindow window(&host); VoicePanel panel(&window); show(panel);
        panel.receive({{"type", "message"}, {"id", "1"}, {"role", "user"}, {"text", "What does the selected function do?"}});
        panel.receive({{"type", "message"}, {"id", "1"}, {"role", "assistant"}, {"text", "It waits for speech above the volume threshold, then sends the recording after a short pause. Brief background sounds are ignored."}});
        panel.receive({{"type", "process"}, {"text", "read_file: voiceactivity.cpp"}});
        panel.receive({{"type", "timing"}, {"elapsed", 3.2}, {"outcome", "completed"}});
        QTRY_VERIFY(js(panel, QStringLiteral("document.body.textContent.includes('Brief background')")).toBool()); QTest::qWait(300);
        QVERIFY(panel.grab().save(qEnvironmentVariable("KATE_VOICE_SCREENSHOT")));
    }
};
int main(int argc, char **argv)
{
    QTemporaryDir config; qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv); app.setApplicationName(QStringLiteral("kate-voice-panel-test"));
    PanelTest test; return QTest::qExec(&test, argc, argv);
}
#include "panel_test.moc"
