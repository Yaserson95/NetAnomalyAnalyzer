#include "LlmClient.h"

LlmClient::LlmClient(QObject *parent)
    : QObject(parent)
{
}

LlmClient::~LlmClient() = default;

void LlmClient::testConnection(const LlmSettings &settings)
{
    Q_UNUSED(settings)
}

void LlmClient::cancel()
{
    m_busy = false;
}
