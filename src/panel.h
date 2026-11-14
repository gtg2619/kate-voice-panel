#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include "voiceactivity.h"

class QToolButton;
class QLineEdit;
class QComboBox;
class QSlider;
class QCheckBox;
class QLabel;
class QSplitter;
class QSpinBox;
class QDoubleSpinBox;
class QProgressBar;
class QWebEngineView;
namespace KTextEditor { class MainWindow; class View; }

class VoicePanel : public QWidget
{
    Q_OBJECT
public:
    explicit VoicePanel(KTextEditor::MainWindow *window, QWidget *parent = nullptr);
    ~VoicePanel() override;
    QString workspace() const;
    QSize sizeHint() const override { return {800, requiredHeight()}; }
    QSize minimumSizeHint() const override { return {0, 0}; }
    int requiredHeight() const;
    QJsonObject editorContext() const;
    void rendererReady();
    void openLink(const QString &href);
    void stop();
    void reset();
    void skipSpeech();
    void receive(const QJsonObject &event);
Q_SIGNALS:
    void transcriptEvent(const QString &json);
    void insufficientSpace();
    void requestSent(const QJsonObject &request);
    void speechPlaybackChanged(bool playing);
protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
private:
    void send(const QJsonObject &request);
    void ask(const QString &text, const QString &audio = {});
    void startBridge();
    void updateTheme();
    void scheduleSpaceCheck();
    void updateControls();
    void saveSettings();
    void queueSetting(const QString &key, const QVariant &value);
    void refreshDevices();
    void play(const QString &path);
    void startListening();
    void pauseListening();
    void publish(const QJsonObject &event);
    KTextEditor::MainWindow *m_window;
    QWebEngineView *m_transcript = nullptr;
    QToolButton *m_new;
    QToolButton *m_skipSpeech;
    QWidget *m_settings = nullptr;
    QWidget *m_controls;
    QSplitter *m_split;
    QToolButton *m_autoInput;
    QSpinBox *m_threshold;
    QSpinBox *m_silence;
    QDoubleSpinBox *m_speed;
    QProgressBar *m_level;
    QComboBox *m_voice;
    QComboBox *m_microphone;
    QComboBox *m_speaker;
    QCheckBox *m_readAloud;
    QSlider *m_volume;
    QLabel *m_scope;
    QProcess m_bridge;
    QProcess m_playback;
    QProcess m_devices;
    QProcess m_listener;
    QTimer m_saveTimer;
    QVariantMap m_pendingSettings;
    VoiceActivity m_activity;
    QTemporaryDir m_directory;
    QByteArray m_buffer;
    QList<QJsonObject> m_events;
    QList<QJsonObject> m_commands;
    QJsonObject m_recordContext;
    QString m_recordWorkspace;
    QString m_sessionWorkspace;
    QString m_request;
    bool m_ready = false;
    bool m_bridgeReady = false;
    bool m_busy = false;
    bool m_closing = false;
    bool m_spaceCheckPending = false;
    bool m_settingsError = false;
};

class VoiceTranscriptBridge : public QObject
{
    Q_OBJECT
public:
    explicit VoiceTranscriptBridge(VoicePanel *panel);
public Q_SLOTS:
    void rendererReady();
    void openLink(const QString &href);
Q_SIGNALS:
    void transcriptEvent(const QString &json);
private:
    VoicePanel *m_panel;
};
