#include "filelink.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

FileLink resolveFileLink(QString input, const QString &workspace)
{
    FileLink result;
    input = input.trimmed();
    const bool encodedReference = input.startsWith(QLatin1String("agent-file:"));
    if (encodedReference) {
        input = QUrl::fromPercentEncoding(input.mid(11).toUtf8());
    }
    // Workspace references (:L12-24), GitHub anchors, and compiler locations.
    static const QRegularExpression location(QStringLiteral(R"((?::L(\d+)(?:-(?:L)?(\d+))?|#L(\d+)(?:C(\d+))?(?:-L(\d+))?|:(\d+)(?::(\d+))?)$)"));
    auto match = location.match(input);
    if (match.hasMatch()) {
        const auto line = !match.captured(1).isEmpty() ? match.captured(1) : (!match.captured(3).isEmpty() ? match.captured(3) : match.captured(6));
        const auto col = !match.captured(4).isEmpty() ? match.captured(4) : match.captured(7);
        const auto end = !match.captured(2).isEmpty() ? match.captured(2) : match.captured(5);
        bool valid = false;
        const int firstLine = line.toInt(&valid);
        if (!valid || firstLine < 1) return result;
        result.line = firstLine - 1;
        if (!col.isEmpty()) {
            const int column = col.toInt(&valid);
            if (!valid || column < 1) return result;
            result.column = column - 1;
        }
        if (!end.isEmpty()) {
            const int lastLine = end.toInt(&valid);
            if (!valid || lastLine < firstLine) return result;
            result.endLine = lastLine - 1;
        }
        input.truncate(match.capturedStart());
    }
    QUrl url(input);
    QString path;
    if (url.scheme() == QLatin1String("file")) {
        if (!url.host().isEmpty() && url.host() != QLatin1String("localhost")) return result;
        path = url.toLocalFile();
    } else if (!url.scheme().isEmpty()) {
        return result;
    } else {
        path = encodedReference ? input : QUrl::fromPercentEncoding(input.toUtf8());
    }
    if (path.contains(QChar::Null) || path.isEmpty()) return result;
    if (path.startsWith(QLatin1String("~/"))) path = QDir::home().filePath(path.mid(2));
    if (QDir::isRelativePath(path)) {
        const QString prefix = QFileInfo(QDir(workspace).absolutePath()).fileName() + QLatin1Char('/');
        if (path.startsWith(prefix)) path = path.mid(prefix.size());
        path = QDir(workspace).absoluteFilePath(path);
    }
    QFileInfo info(QDir::cleanPath(path));
    if (!info.isFile()) return result;
    result.url = QUrl::fromLocalFile(info.absoluteFilePath());
    result.valid = true;
    return result;
}

QString workspaceReference(const QString &file, const QString &workspace, int firstLine, int lastLine)
{
    const QDir root(workspace);
    QString path = root.relativeFilePath(file);
    path = QFileInfo(root.absolutePath()).fileName() + QLatin1Char('/') + path;
    path += QStringLiteral(":L%1").arg(firstLine + 1);
    if (lastLine > firstLine) path += QStringLiteral("-%1").arg(lastLine + 1);
    return path;
}
