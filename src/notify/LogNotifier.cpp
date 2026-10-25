#include "LogNotifier.h"
#include "../logging/LogWriter.h"

LogNotifier::LogNotifier(LogWriter *logWriter, QObject *parent)
    : Notifier(parent)
    , m_logWriter(logWriter)
{
}

void LogNotifier::notify(const Anomaly &anomaly)
{
    if (m_logWriter)
        m_logWriter->logNotification(channelName(), anomaly, QStringLiteral("ok"));
    emit sent(channelName(), tr("записано"));
}
