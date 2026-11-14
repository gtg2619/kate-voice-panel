#include "panel.h"
#include <KActionCollection>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KTextEditor/MainWindow>
#include <KTextEditor/Plugin>
#include <KTextEditor/View>
#include <KXMLGUIClient>
#include <KXMLGUIFactory>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QIcon>
#include <QMenu>
#include <QKeyEvent>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

class VoicePluginView : public QObject, public KXMLGUIClient
{
    Q_OBJECT
public:
    VoicePluginView(KTextEditor::Plugin *plugin, KTextEditor::MainWindow *window)
        : QObject(window), m_window(window)
    {
        const auto agentIcon = QIcon::fromTheme(QStringLiteral("audio-input-microphone"),
            QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation));
        m_tool = window->createToolView(plugin, QStringLiteral("kate_voice_panel"),
            KTextEditor::MainWindow::Bottom, agentIcon, i18n("Voice"));
        if (!m_tool) return;
        m_tool->installEventFilter(this);
        if (m_tool->isVisible()) schedulePanel();
        qApp->installEventFilter(this);
        setComponentName(QStringLiteral("katevoicepanel"), i18n("Voice Panel"));
        setXMLFile(QStringLiteral(":/voicepanel/ui.rc"));

        auto show = actionCollection()->addAction(QStringLiteral("voice_panel_show"));
        show->setIcon(agentIcon);
        show->setText(i18n("Show Voice Panel"));
        connect(show, &QAction::triggered, this, [this] {
            m_window->showToolView(m_tool);
            ensurePanel()->setFocus();
        });
        if (window->guiFactory()) window->guiFactory()->addClient(this);
    }

    ~VoicePluginView() override
    {
        if (m_window->guiFactory()) m_window->guiFactory()->removeClient(this);
        // Tool view is owned by the window; explicitly remove it when the plugin is disabled.
        delete m_tool;
    }

private:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == m_tool && event->type() == QEvent::Show && !m_panel) schedulePanel();
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent *>(event);
            const auto target = qobject_cast<QWidget *>(object);
            const auto focus = target ? target : QApplication::focusWidget();
            // Native tool surfaces must not steal Escape from modal dialogs or menus.
            if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier
                && m_tool && m_tool->isVisible() && QApplication::activeWindow() == m_tool->window()
                && focus && focus->window() == m_tool->window()
                && (focus == m_tool || m_tool->isAncestorOf(focus))
                && !QApplication::activeModalWidget() && !QApplication::activePopupWidget()) {
                event->accept();
                if (event->type() == QEvent::KeyPress) {
                    m_window->hideToolView(m_tool);
                    if (auto view = m_window->activeView()) view->setFocus();
                }
                return true;
            }
        }
        return QObject::eventFilter(object, event);
    }
    void schedulePanel()
    {
        const auto host = m_tool->window()->windowHandle();
        const bool focusComposer = host && host->isExposed();
        // Session restoration can show tools while Kate is still constructing its
        // window. Leave that stack before initializing WebEngine and the bridge.
        QTimer::singleShot(0, this, [this, focusComposer] {
            if (!m_tool || !m_tool->isVisible()) return;
            auto panel = ensurePanel();
            // A click in an existing window should focus the newly created input.
            // Restoring a tool during startup must leave the editor's focus alone.
            if (focusComposer) panel->setFocus();
        });
    }
    VoicePanel *ensurePanel()
    {
        if (m_panel) return m_panel;
        // Kate owns this layout and may automatically insert child widgets.
        auto layout = m_tool->layout();
        if (!layout) {
            layout = new QVBoxLayout(m_tool);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_panel = new VoicePanel(m_window, m_tool);
        connect(m_panel, &VoicePanel::insufficientSpace, this, [this] {
            m_window->hideToolView(m_tool);
            if (auto view = m_window->activeView()) view->setFocus();
        });
        if (layout->indexOf(m_panel) < 0) layout->addWidget(m_panel);
        m_panel->show();
        return m_panel;
    }
    KTextEditor::MainWindow *m_window;
    QPointer<QWidget> m_tool;
    QPointer<VoicePanel> m_panel;
};

class VoicePlugin : public KTextEditor::Plugin
{
    Q_OBJECT
public:
    explicit VoicePlugin(QObject *parent) : KTextEditor::Plugin(parent) {}
    QObject *createView(KTextEditor::MainWindow *window) override { return new VoicePluginView(this, window); }
};

K_PLUGIN_FACTORY_WITH_JSON(VoicePluginFactory, "plugin.json", registerPlugin<VoicePlugin>();)
#include "plugin.moc"
