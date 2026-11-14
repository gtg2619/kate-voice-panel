#pragma once
#include <QObject>
#include <QByteArray>

// Energy gating for PipeWire's mono 16 kHz signed 16-bit little-endian stream.
// No model, network request or audio upload occurs until an utterance completes.
class VoiceActivity : public QObject
{
    Q_OBJECT
public:
    explicit VoiceActivity(QObject *parent = nullptr) : QObject(parent) {}
    void setThreshold(int db) { m_threshold = qBound(-60, db, -10); }
    void setSilence(int ms) { m_silenceLimit = qBound(400, ms, 2000); }
    void feed(const QByteArray &pcm);
    void reset();
    static QByteArray wav(const QByteArray &pcm);
Q_SIGNALS:
    void levelChanged(int db);
    void speechStarted();
    void speechDiscarded();
    void utterance(const QByteArray &pcm);
private:
    QByteArray m_pending;
    QByteArray m_preroll;
    QByteArray m_speech;
    int m_threshold = -35;
    int m_silenceLimit = 900;
    int m_attack = 0;
    int m_silence = 0;
    int m_voiced = 0;
    bool m_active = false;
};
