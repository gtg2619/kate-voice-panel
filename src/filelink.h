#pragma once
#include <QString>
#include <QUrl>

struct FileLink {
    QUrl url;
    int line = 0;
    int column = 0;
    int endLine = -1;
    bool valid = false;
};

// Accept only local file paths, resolving relative references against the session workspace.
FileLink resolveFileLink(QString input, const QString &workspace);
QString workspaceReference(const QString &file, const QString &workspace, int firstLine, int lastLine);
