#include "filelink.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class FileLinkTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void references()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + QStringLiteral("/project");
        QDir().mkpath(root + QStringLiteral("/src"));
        const auto path = root + QStringLiteral("/src/a file.cpp");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("text\n"); file.close();
        QCOMPARE(workspaceReference(path, root, 122, 233), QStringLiteral("project/src/a file.cpp:L123-234"));
        QCOMPARE(workspaceReference(path, root, 122, 122), QStringLiteral("project/src/a file.cpp:L123"));
        auto link = resolveFileLink(QStringLiteral("project/src/a file.cpp:L123-234"), root);
        QVERIFY(link.valid); QCOMPARE(link.url.toLocalFile(), path); QCOMPARE(link.line, 122); QCOMPARE(link.endLine, 233);
        link = resolveFileLink(QStringLiteral("agent-file:") + QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("project/src/a file.cpp:L3-7"))), root);
        QVERIFY(link.valid); QCOMPARE(link.line, 2); QCOMPARE(link.endLine, 6);
        link = resolveFileLink(QStringLiteral("src/a file.cpp:9:4"), root);
        QVERIFY(link.valid); QCOMPARE(link.line, 8); QCOMPARE(link.column, 3);
        link = resolveFileLink(QUrl::fromLocalFile(path).toString() + QStringLiteral("#L2-L5"), root);
        QVERIFY(link.valid); QCOMPARE(link.line, 1); QCOMPARE(link.endLine, 4);
        QVERIFY(!resolveFileLink(QStringLiteral("javascript:alert(1)"), root).valid);
        QVERIFY(!resolveFileLink(QStringLiteral("file://remote/etc/passwd"), root).valid);
        QVERIFY(!resolveFileLink(QStringLiteral("missing.cpp:L1"), root).valid);
        QVERIFY(!resolveFileLink(QStringLiteral("src"), root).valid);
        QFile extensionless(root + QStringLiteral("/src/file"));
        QVERIFY(extensionless.open(QIODevice::WriteOnly)); extensionless.close();
        QVERIFY(resolveFileLink(QStringLiteral("project/src/file:L123-234"), root).valid);
        QFile literalPercent(root + QStringLiteral("/src/a%20file.cpp"));
        QVERIFY(literalPercent.open(QIODevice::WriteOnly)); literalPercent.close();
        link = resolveFileLink(QStringLiteral("agent-file:project%2Fsrc%2Fa%2520file.cpp%3AL1"), root);
        QCOMPARE(link.url.toLocalFile(), literalPercent.fileName());
    }

    void unicodeAndEncodedReferences()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + QStringLiteral("/\u9879\u76ee");
        QDir().mkpath(root + QStringLiteral("/\u76ee\u5f55"));
        const auto relative = QStringLiteral("\u76ee\u5f55/\u4e2d\u6587 file%20+#.cpp");
        const auto filename = root + QLatin1Char('/') + relative;
        QFile file(filename); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        const auto reference = workspaceReference(filename, root, 122, 233);
        QCOMPARE(reference, QStringLiteral("\u9879\u76ee/") + relative + QStringLiteral(":L123-234"));
        auto result = resolveFileLink(QStringLiteral("agent-file:") + QString::fromLatin1(QUrl::toPercentEncoding(reference)), root);
        QVERIFY(result.valid); QCOMPARE(result.url.toLocalFile(), filename);
        QCOMPARE(result.line, 122); QCOMPARE(result.endLine, 233);
        result = resolveFileLink(QString::fromLatin1(QUrl::toPercentEncoding(relative, "/")) + QStringLiteral("#L3C2-L5"), root);
        QVERIFY(result.valid); QCOMPARE(result.url.toLocalFile(), filename);
        QCOMPARE(result.line, 2); QCOMPARE(result.column, 1); QCOMPARE(result.endLine, 4);
        result = resolveFileLink(QUrl::fromLocalFile(filename).toString(QUrl::FullyEncoded) + QStringLiteral("#L7-L9"), root);
        QVERIFY(result.valid); QCOMPARE(result.url.toLocalFile(), filename);
        QCOMPARE(result.line, 6); QCOMPARE(result.endLine, 8);
        result = resolveFileLink(QStringLiteral("agent-file:") + QString::fromLatin1(QUrl::toPercentEncoding(filename + QStringLiteral(":12:4"))), root);
        QVERIFY(result.valid); QCOMPARE(result.url.toLocalFile(), filename);
        QCOMPARE(result.line, 11); QCOMPARE(result.column, 3);
        for (const auto &invalid : {QStringLiteral("https://example.com/a.cpp:L1"), QStringLiteral("data:text/html,test"),
                 QStringLiteral("agent-file:project%2Fmissing.cpp%00:L1"), QStringLiteral("file://remote/secret")}) {
            QVERIFY(!resolveFileLink(invalid, root).valid);
        }
    }

    void generatedReferenceRoundTrips()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + QStringLiteral("/project");
        QDir().mkpath(root + QStringLiteral("/src"));
        const QStringList names = {QStringLiteral("plain"), QStringLiteral("a file.cpp"), QStringLiteral("literal%20.cpp"),
            QStringLiteral("hash#question?.cpp"), QStringLiteral("plus+equals=.cpp"), QStringLiteral("\u4e2d\u6587.cpp")};
        for (const auto &name : names) {
            const auto filename = root + QStringLiteral("/src/") + name;
            QFile file(filename); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
            for (int start = 0; start < 40; ++start) {
                const auto reference = workspaceReference(filename, root, start, start + 7);
                const auto url = QStringLiteral("agent-file:") + QString::fromLatin1(QUrl::toPercentEncoding(reference));
                const auto link = resolveFileLink(url, root);
                QVERIFY2(link.valid, qPrintable(reference));
                QCOMPARE(link.url.toLocalFile(), filename);
                QCOMPARE(link.line, start); QCOMPARE(link.endLine, start + 7);
            }
        }
    }

    void invalidLocationDoesNotJumpToFirstLine()
    {
        QTemporaryDir temp;
        QFile file(temp.path() + QStringLiteral("/a.cpp")); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        for (const auto &suffix : {QStringLiteral(":L0"), QStringLiteral(":L9999999999999999999999"),
                 QStringLiteral(":2:9999999999999999"), QStringLiteral(":L4-2"), QStringLiteral("#L4C0")}) {
            QVERIFY2(!resolveFileLink(QStringLiteral("a.cpp") + suffix, temp.path()).valid, qPrintable(suffix));
        }
    }
};
QTEST_GUILESS_MAIN(FileLinkTest)
#include "filelink_test.moc"
