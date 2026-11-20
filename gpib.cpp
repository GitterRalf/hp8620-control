// GPIB transport adapted from the OsciViewer/TR4131-Viewer projects.
#include "gpib.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLibrary>
#include <QSerialPort>

#include <array>
#include <stdexcept>
#include <windows.h>

namespace {

[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

class NiGpib final : public GpibConnection
{
    Q_DECLARE_TR_FUNCTIONS(NiGpib)

public:
    explicit NiGpib(const GpibTarget &target) : board(target.board), address(target.address)
    {
        wchar_t systemDirectory[MAX_PATH]{};
        const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (!length || length >= MAX_PATH) {
            fail(tr("Das Windows-Systemverzeichnis konnte nicht ermittelt werden."));
        }
        library.setFileName(QDir(QString::fromWCharArray(systemDirectory)).filePath(
            QStringLiteral("ni4882.dll")));
        if (!library.load()) {
            fail(tr("NI-488.2 konnte nicht geladen werden. %1").arg(library.errorString()));
        }

        const auto open = resolve<int (WINAPI *)(int, int, int, int, int, int)>("ibdev");
        const auto listen = resolve<int (WINAPI *)(int, int, int, short *)>("ibln");
        offline = resolve<int (WINAPI *)(int, int)>("ibonl");
        setLocal = resolve<int (WINAPI *)(int)>("ibloc");
        enableRemote = resolve<void (WINAPI *)(int, const unsigned short *)>("EnableRemote");
        write = resolve<int (WINAPI *)(int, const void *, size_t)>("ibwrt");
        status = resolve<int (WINAPI *)()>("ThreadIbsta");
        error = resolve<int (WINAPI *)()>("ThreadIberr");
        count = resolve<int (WINAPI *)()>("ThreadIbcnt");

        constexpr int timeout1Second = 11;
        descriptor = open(board, address, 0, timeout1Second, 1, 0);
        if (descriptor < 0) {
            fail(tr("GPIB %1, Adresse %2 konnte nicht geöffnet werden (NI-Fehler %3).")
                 .arg(board).arg(address).arg(error()));
        }
        try {
            short present = 0;
            check(listen(descriptor, address, 0, &present), tr("GPIB-Adressprüfung"));
            if (!present) {
                fail(tr("Auf GPIB %1, Adresse %2 antwortet kein Teilnehmer.")
                     .arg(board).arg(address));
            }
        } catch (...) {
            offline(descriptor, 0);
            throw;
        }
    }

    ~NiGpib() override { offline(descriptor, 0); }

    void remote() override
    {
        const std::array<unsigned short, 2> addresses{static_cast<unsigned short>(address), 0xffff};
        enableRemote(board, addresses.data());
        check(status(), tr("Umschalten auf Fernsteuerung"));
    }

    void local() override
    {
        check(setLocal(descriptor), tr("Rückgabe an die Gerätebedienung"));
    }

    void send(const QByteArray &command) override
    {
        check(write(descriptor, command.constData(), static_cast<size_t>(command.size())),
              tr("Senden von %1").arg(QString::fromLatin1(command)));
        if (count() != command.size()) {
            fail(tr("Der GPIB-Befehl wurde nur teilweise übertragen."));
        }
    }

private:
    template<typename Function>
    Function resolve(const char *name)
    {
        const auto symbol = library.resolve(name);
        if (!symbol) {
            fail(tr("NI-488.2 stellt %1 nicht bereit.").arg(QString::fromLatin1(name)));
        }
        return reinterpret_cast<Function>(symbol);
    }

    void check(int state, const QString &operation) const
    {
        if (state & 0x8000) {
            fail(tr("%1 fehlgeschlagen (NI-Fehler %2, Status 0x%3).")
                 .arg(operation).arg(error()).arg(state & 0xffff, 0, 16));
        }
    }

    QLibrary library;
    const int board;
    const int address;
    int descriptor = -1;
    int (WINAPI *offline)(int, int) = nullptr;
    int (WINAPI *setLocal)(int) = nullptr;
    void (WINAPI *enableRemote)(int, const unsigned short *) = nullptr;
    int (WINAPI *write)(int, const void *, size_t) = nullptr;
    int (WINAPI *status)() = nullptr;
    int (WINAPI *error)() = nullptr;
    int (WINAPI *count)() = nullptr;
};

class ElektorGpib final : public GpibConnection
{
    Q_DECLARE_TR_FUNCTIONS(ElektorGpib)

public:
    explicit ElektorGpib(const GpibTarget &target)
        : address(target.address), addressText(QByteArray::number(address))
    {
        serial.setPortName(target.serialPort);
        serial.setBaudRate(38400);
        serial.setDataBits(QSerialPort::Data8);
        serial.setParity(QSerialPort::NoParity);
        serial.setStopBits(QSerialPort::OneStop);
        serial.setFlowControl(QSerialPort::NoFlowControl);
        if (!serial.open(QIODevice::ReadWrite)) {
            serialError(tr("Öffnen von %1").arg(target.serialPort));
        }
        serial.clear(QSerialPort::Input);
        const QByteArray identity = command("I", 1500).simplified().toUpper();
        if (!identity.contains("GPIB/USB CONVERTER") && !identity.contains("GPIB-TO-USB CONVERTER")
            && !identity.contains("GPIB TO USB CONVERTER")) {
            fail(tr("An %1 wurde keine Elektor-GPIB-Firmware erkannt.").arg(target.serialPort));
        }
        // The firmware enables REN at startup. Addressing the instrument enters remote mode.
        // OsciViewer's firmware uses 100,000 ticks per second.
        command("T10000");
    }

    void remote() override {}

    void local() override
    {
        command("G" + QByteArray::number(0x20 + address));
        try {
            command("G1");
        } catch (...) {
            try { command("G63"); } catch (...) {}
            throw;
        }
        command("G63");
    }

    void send(const QByteArray &payload) override
    {
        command("W" + addressText + ',' + payload);
    }

private:
    [[noreturn]] void serialError(const QString &operation) const
    {
        fail(tr("%1 über Elektor fehlgeschlagen. %2").arg(operation, serial.errorString()));
    }

    QByteArray command(const QByteArray &payload, int replyTimeout = 130)
    {
        const QByteArray frame = payload + "\r\n";
        if (serial.write(frame) != frame.size()) {
            serialError(tr("Schreiben"));
        }
        QElapsedTimer timer;
        timer.start();
        while (serial.bytesToWrite() > 0) {
            const auto remaining = 500 - timer.elapsed();
            if (remaining <= 0 || !serial.waitForBytesWritten(static_cast<int>(remaining))) {
                serialError(tr("Schreiben"));
            }
        }
        QByteArray reply;
        timer.restart();
        while (timer.elapsed() < replyTimeout && !reply.contains('\n')) {
            if (serial.bytesAvailable() == 0) {
                const int wait = reply.isEmpty() ? replyTimeout - static_cast<int>(timer.elapsed()) : 20;
                if (wait <= 0 || !serial.waitForReadyRead(wait)) {
                    if (serial.error() != QSerialPort::TimeoutError && serial.error() != QSerialPort::NoError) {
                        serialError(tr("Lesen"));
                    }
                    break;
                }
            }
            reply += serial.readAll();
        }
        if (reply.trimmed().toUpper().startsWith("ERROR")) {
            fail(tr("Elektor meldet %1.").arg(QString::fromLatin1(reply.trimmed())));
        }
        return reply;
    }

    const int address;
    const QByteArray addressText;
    QSerialPort serial;
};

} // namespace

std::unique_ptr<GpibConnection> openGpib(const GpibTarget &target)
{
    if (target.elektor) {
        return std::make_unique<ElektorGpib>(target);
    }
    return std::make_unique<NiGpib>(target);
}
