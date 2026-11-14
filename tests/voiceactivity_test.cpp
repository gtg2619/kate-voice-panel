#include "voiceactivity.h"
#include <QSignalSpy>
#include <QTest>
#include <QtEndian>

class VoiceActivityTest : public QObject
{
    Q_OBJECT
    QByteArray audio(int ms, int amplitude) {
        QByteArray samples(ms * 32, '\0');
        for (qsizetype i = 0; i < samples.size(); i += 2) qToLittleEndian<qint16>(i % 4 ? amplitude : -amplitude, samples.data() + i);
        return samples;
    }
private Q_SLOTS:
    void quietAudioDoesNotTrigger() {
        VoiceActivity gate; QSignalSpy started(&gate, &VoiceActivity::speechStarted), utterance(&gate, &VoiceActivity::utterance);
        gate.feed(audio(10000, 100)); QCOMPARE(started.count(), 0); QCOMPARE(utterance.count(), 0);
    }
    void thresholdStartsAfterAttackAndFinishesAfterSilence() {
        VoiceActivity gate; QSignalSpy started(&gate, &VoiceActivity::speechStarted), utterance(&gate, &VoiceActivity::utterance);
        gate.feed(audio(200, 0)); gate.feed(audio(40, 6000)); QCOMPARE(started.count(), 0);
        gate.feed(audio(20, 6000)); QCOMPARE(started.count(), 1);
        gate.feed(audio(440, 6000)); gate.feed(audio(880, 0)); QCOMPARE(utterance.count(), 0);
        gate.feed(audio(20, 0)); QCOMPARE(utterance.count(), 1);
        const auto pcm = utterance.at(0).at(0).toByteArray(); QVERIFY(pcm.size() >= 1400 * 32); QVERIFY(pcm.size() <= 1700 * 32);
    }
    void briefNoiseIsDiscarded() {
        VoiceActivity gate; QSignalSpy utterance(&gate, &VoiceActivity::utterance), discarded(&gate, &VoiceActivity::speechDiscarded);
        gate.feed(audio(100, 20000)); gate.feed(audio(1000, 0)); QCOMPARE(utterance.count(), 0); QCOMPARE(discarded.count(), 1);
    }
    void settingsControlThresholdAndSilence() {
        VoiceActivity gate; gate.setThreshold(-20); gate.setSilence(400);
        QSignalSpy utterance(&gate, &VoiceActivity::utterance);
        gate.feed(audio(1000, 1000)); gate.feed(audio(500, 0)); QCOMPARE(utterance.count(), 0);
        gate.setThreshold(-40); gate.feed(audio(300, 1000)); gate.feed(audio(400, 0)); QCOMPARE(utterance.count(), 1);
    }
    void fragmentedSamplesMatchContiguousFrames() {
        VoiceActivity gate; QSignalSpy utterance(&gate, &VoiceActivity::utterance);
        const auto input = audio(500, 7000) + audio(900, 0);
        for (qsizetype i = 0; i < input.size(); i += 137) gate.feed(input.mid(i, 137));
        QCOMPARE(utterance.count(), 1);
    }
    void resetDropsCapturedSpeech() {
        VoiceActivity gate; QSignalSpy utterance(&gate, &VoiceActivity::utterance);
        gate.feed(audio(500, 7000)); gate.reset(); gate.feed(audio(1000, 0)); QCOMPARE(utterance.count(), 0);
        gate.feed(audio(500, 7000)); gate.feed(audio(900, 0)); QCOMPARE(utterance.count(), 1);
    }
    void longSpeechIsBoundedBelowApiLimit() {
        VoiceActivity gate; QSignalSpy utterance(&gate, &VoiceActivity::utterance);
        gate.feed(audio(111000, 7000)); QCOMPARE(utterance.count(), 1);
        QCOMPARE(utterance.at(0).at(0).toByteArray().size(), 110000 * 32);
    }
    void wavHeaderMatchesPcm() {
        const auto pcm = audio(1000, 3000), wav = VoiceActivity::wav(pcm);
        QCOMPARE(wav.first(4), QByteArray("RIFF")); QCOMPARE(wav.mid(8, 8), QByteArray("WAVEfmt "));
        QCOMPARE(qFromLittleEndian<quint32>(wav.constData() + 24), 16000u);
        QCOMPARE(qFromLittleEndian<quint32>(wav.constData() + 40), quint32(pcm.size())); QCOMPARE(wav.mid(44), pcm);
    }
};
QTEST_GUILESS_MAIN(VoiceActivityTest)
#include "voiceactivity_test.moc"
