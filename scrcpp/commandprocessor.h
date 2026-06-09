#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <cstdint>

class CommandProcessor : public QObject
{
    Q_OBJECT

public:
    using SimpleCallback = std::function<QString()>;
    using MemoryReadCallback = std::function<bool(uint32_t address, uint8_t& value)>;
    using DisasmCallback = std::function<QString(uint16_t from, uint16_t to)>;
    using BreakpointCallback = std::function<QString(uint16_t address, bool enabled)>;
    using MemoryWriteCallback = std::function<bool(uint32_t address, uint8_t value)>;

    explicit CommandProcessor(QObject* parent = nullptr);

    QString execute(const QString& commandLine);

    void setStartCallback(SimpleCallback cb)       { m_startCb = std::move(cb); }
    void setStopCallback(SimpleCallback cb)        { m_stopCb = std::move(cb); }
    void setPauseCallback(SimpleCallback cb)       { m_pauseCb = std::move(cb); }
    void setRunCallback(SimpleCallback cb)         { m_runCb = std::move(cb); }
    void setResetAdamCallback(SimpleCallback cb)   { m_resetAdamCb = std::move(cb); }
    void setResetColecoCallback(SimpleCallback cb) { m_resetColecoCb = std::move(cb); }
    void setPowerOffCallback(SimpleCallback cb)    { m_powerOffCb = std::move(cb); }
    void setCpuRegsCallback(SimpleCallback cb)     { m_cpuRegsCb = std::move(cb); }

    void setMemoryReadCallback(MemoryReadCallback cb) { m_memoryReadCb = std::move(cb); }
    void setDisasmCallback(DisasmCallback cb)         { m_disasmCb = std::move(cb); }
    void setMemoryWriteCallback(MemoryWriteCallback cb) { m_memoryWriteCb = std::move(cb); }

private:
    QString cmdHelp() const;
    QString cmdMemory(const QStringList& args) const;
    QString cmdDisasm(const QStringList& args) const;
    QString cmdBreakpoint(const QStringList& args) const;
    QString cmdPeek(const QStringList& args) const;

    bool parseAddress(const QString& text, uint32_t& value, uint32_t maxValue = 0xFFFF) const;
    QString formatHexDump(uint32_t from, uint32_t to) const;
    MemoryWriteCallback m_memoryWriteCb;
    QString cmdPoke(const QStringList& args) const;

private:
    SimpleCallback m_startCb;
    SimpleCallback m_stopCb;
    SimpleCallback m_pauseCb;
    SimpleCallback m_runCb;
    SimpleCallback m_resetAdamCb;
    SimpleCallback m_resetColecoCb;
    SimpleCallback m_powerOffCb;
    SimpleCallback m_cpuRegsCb;

    MemoryReadCallback m_memoryReadCb;
    DisasmCallback m_disasmCb;
};
