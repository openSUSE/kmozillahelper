/*****************************************************************

Copyright (C) 2009 Lubos Lunak <l.lunak@suse.cz>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

******************************************************************/

#ifndef MAIN_H
#define MAIN_H

#include <QtCore/QMimeType>
#include <QtCore/QSocketNotifier>

class Helper : public QObject
{
    Q_OBJECT
public:
    Helper();
private:
    bool handleCheck();
    bool handleGetProxy();
    bool handleHandlerExists();
    bool handleGetFromExtension();
    bool handleGetFromType();
    bool handleGetAppDescForScheme();
    bool handleAppsDialog();
    bool handleGetOpenOrSaveX(bool url, bool save);
    bool handleGetDirectoryX(bool url);
    bool handleOpen();
    bool handleReveal();
    bool handleRun();
    bool handleGetDefaultFeedReader();
    bool handleOpenMail();
    bool handleOpenNews();
    bool handleIsDefaultBrowser();
    bool handleSetDefaultBrowser();
    bool handleDownloadFinished();
    QStringList convertToNameFilters(const QString &input);
    bool writeMimeInfo(QMimeType mime);
    QString getAppForProtocol(const QString& protocol);
    bool readArguments(int mincount);
    QString getArgument();
    bool isArgument(const QString& name); // also discards the line with it
    bool allArgumentsUsed();
    long getArgumentParent();
    void outputLine(QString line, bool escape = true);
    QString readLine();
protected:
    virtual bool eventFilter(QObject *obj, QEvent *ev) override;
private slots:
    void readCommand();
private:
    QSocketNotifier notifier;
    QStringList arguments;
    bool arguments_read;
    long wid;
};

#endif
