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
#include <QMouseEvent>
#include <QKeyEvent>
#include <QStyle>
#include <QSplitter>
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
        m_skipSpeech = actionCollection()->addAction(QStringLiteral("voice_panel_skip_speech"));
        m_skipSpeech->setText(i18n("Skip Voice Reply"));
        m_skipSpeech->setEnabled(false);
        KActionCollection::setDefaultShortcut(m_skipSpeech, Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Space);
        m_tool->window()->addAction(m_skipSpeech);
        connect(m_skipSpeech, &QAction::triggered, this, [this] { if (m_panel) m_panel->skipSpeech(); });
        if (window->guiFactory()) window->guiFactory()->addClient(this);
    }

    ~VoicePluginView() override
    {
        endDockDrag();
        if (m_window->guiFactory()) m_window->guiFactory()->removeClient(this);
        // Tool view is owned by the window; explicitly remove it when the plugin is disabled.
        delete m_tool;
    }

private:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == m_tool && event->type() == QEvent::Show) schedulePanel();
        if (m_dragSurface && ((event->type() == QEvent::MouseButtonRelease
                && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
            || event->type() == QEvent::ApplicationDeactivate
            || (object == m_dragHandle && event->type() == QEvent::UngrabMouse)
            || (object == m_tool && event->type() == QEvent::Hide)
            || (m_tool && object == m_tool->window() && event->type() == QEvent::WindowDeactivate))) {
            endDockDrag();
        }
        if (event->type() == QEvent::MouseButtonPress && m_panel && m_tool->isVisible()
            && QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            auto handle = qobject_cast<QSplitterHandle *>(object);
            if (handle && handle->splitter()->isAncestorOf(m_tool) && m_panel->windowHandle()) {
                endDockDrag();
                // Qt/Wayland loses parent-splitter motion over the native RHI surface.
                // Let the dock retain pointer input until the drag ends, while still painting the panel.
                m_dragHandle = handle; m_dragSurface = m_panel->windowHandle();
                m_dragWasTransparent = m_dragSurface->flags().testFlag(Qt::WindowTransparentForInput);
                m_dragSurface->setFlag(Qt::WindowTransparentForInput, true);
            }
        }
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
    void endDockDrag()
    {
        if (m_dragSurface) m_dragSurface->setFlag(Qt::WindowTransparentForInput, m_dragWasTransparent);
        m_dragSurface.clear(); m_dragHandle.clear();
    }
    void schedulePanel()
    {
        if (m_restoringPanel) return;
        m_restoringPanel = true;
        const auto host = m_tool->window()->windowHandle();
        const bool focusComposer = host && host->isExposed();
        // Session restoration can show tools while Kate is still constructing its
        // window. Leave that stack before initializing WebEngine and the bridge.
        QTimer::singleShot(0, this, [this, focusComposer] {
            if (!m_tool || !m_tool->isVisible()) { m_restoringPanel = false; return; }
            auto panel = ensurePanel();
            fitDockHeight();
            m_restoringPanel = false;
            if (panel->height() < panel->requiredHeight()) { hidePanel(); return; }
            // A click in an existing window should focus the newly created input.
            // Restoring a tool during startup must leave the editor's focus alone.
            if (focusComposer) panel->setFocus();
        });
    }
    void fitDockHeight()
    {
        m_tool->layout()->activate();
        // Kate remembers even the undersized height that caused automatic hiding.
        // On opening only, borrow space from adjacent panes so the controls fit again.
        QWidget *branch = m_tool;
        for (auto parent = branch->parentWidget(); parent; branch = parent, parent = parent->parentWidget()) {
            auto splitter = qobject_cast<QSplitter *>(parent);
            if (!splitter || splitter->orientation() != Qt::Vertical) continue;
            const int index = splitter->indexOf(branch);
            if (index < 0) continue;
            int needed = m_panel->requiredHeight() + 8 - m_panel->height();
            if (needed <= 0) break;
            auto sizes = splitter->sizes();
            for (int i = 0; i < sizes.size() && needed > 0; ++i) {
                if (i == index) continue;
                auto sibling = splitter->widget(i);
                const int minimum = qMax(sibling->minimumHeight(), sibling->minimumSizeHint().height());
                const int take = qMin(needed, qMax(0, sizes[i] - minimum));
                sizes[i] -= take; sizes[index] += take; needed -= take;
            }
            splitter->setSizes(sizes);
            m_tool->layout()->activate();
        }
    }
    void hidePanel()
    {
        m_window->hideToolView(m_tool);
        if (auto view = m_window->activeView()) view->setFocus();
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
        // Kate's ToolView prefers its current size. Do not turn a restored or
        // enlarged size into a dock constraint; the panel handles low-height hiding.
        layout->setSizeConstraint(QLayout::SetNoConstraint);
        m_tool->setMinimumHeight(1);
        m_panel = new VoicePanel(m_window, m_tool);
        m_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        connect(m_panel, &VoicePanel::speechPlaybackChanged, m_skipSpeech, &QAction::setEnabled);
        connect(m_panel, &VoicePanel::insufficientSpace, this, [this] {
            if (!m_restoringPanel) hidePanel();
        });
        if (layout->indexOf(m_panel) < 0) layout->addWidget(m_panel);
        m_panel->show();
        return m_panel;
    }
    KTextEditor::MainWindow *m_window;
    QPointer<QWidget> m_tool;
    QPointer<VoicePanel> m_panel;
    QAction *m_skipSpeech = nullptr;
    QPointer<QWindow> m_dragSurface;
    QPointer<QSplitterHandle> m_dragHandle;
    bool m_dragWasTransparent = false;
    bool m_restoringPanel = false;
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
