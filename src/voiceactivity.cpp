#include "voiceactivity.h"
#include <QtEndian>
#include <cmath>

void VoiceActivity::reset()
{
    m_pending.clear(); m_preroll.clear(); m_speech.clear();
    m_attack = 0; m_silence = 0; m_voiced = 0; m_active = false;
}

void VoiceActivity::feed(const QByteArray &pcm)
{
    m_pending += pcm;
    constexpr int frameBytes = 640; // 20 ms
    qsizetype consumed = 0;
    while (m_pending.size() - consumed >= frameBytes) {
        const auto frame = m_pending.mid(consumed, frameBytes); consumed += frameBytes;
        double squares = 0;
        for (int i = 0; i < frameBytes; i += 2) {
            const double sample = qFromLittleEndian<qint16>(frame.constData() + i) / 32768.0;
            squares += sample * sample;
        }
        const int db = qBound(-60, int(std::lround(20 * std::log10(qMax(1e-6, std::sqrt(squares / 320))))), 0);
        Q_EMIT levelChanged(db);
        const bool loud = db >= m_threshold;
        if (!m_active) {
            m_preroll += frame; if (m_preroll.size() > 9600) m_preroll.remove(0, m_preroll.size() - 9600);
            m_attack = loud ? m_attack + 20 : 0;
            if (m_attack < 60) continue;
            m_active = true; m_speech = m_preroll; m_preroll.clear(); m_voiced = m_attack; m_silence = 0;
            Q_EMIT speechStarted();
        } else {
            m_speech += frame;
            if (loud) { m_voiced += 20; m_silence = 0; } else m_silence += 20;
        }
        if (m_silence >= m_silenceLimit || m_speech.size() >= 16000 * 2 * 110) {
            const auto speech = m_speech; const bool valid = m_voiced >= 200;
            // Drop pending frames from this batch once a complete utterance is handed off.
            reset();
            if (valid) Q_EMIT utterance(speech); else Q_EMIT speechDiscarded();
            return;
        }
    }
    m_pending.remove(0, consumed);
}

QByteArray VoiceActivity::wav(const QByteArray &pcm)
{
    QByteArray header(44, '\0');
    header.replace(0, 4, "RIFF"); header.replace(8, 8, "WAVEfmt "); header.replace(36, 4, "data");
    qToLittleEndian<quint32>(pcm.size() + 36, header.data() + 4);
    qToLittleEndian<quint32>(16, header.data() + 16); qToLittleEndian<quint16>(1, header.data() + 20);
    qToLittleEndian<quint16>(1, header.data() + 22); qToLittleEndian<quint32>(16000, header.data() + 24);
    qToLittleEndian<quint32>(32000, header.data() + 28); qToLittleEndian<quint16>(2, header.data() + 32);
    qToLittleEndian<quint16>(16, header.data() + 34); qToLittleEndian<quint32>(pcm.size(), header.data() + 40);
    return header + pcm;
}
