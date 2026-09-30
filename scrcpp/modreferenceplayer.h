#ifndef MODREFERENCEPLAYER_H
#define MODREFERENCEPLAYER_H

#include <QObject>
#include <QIODevice>
#include <QByteArray>
#include <QVector>
#include <QtMultimedia/QAudioSink>
#include <QtMultimedia/QMediaDevices>

class ModReferencePlayer final : public QObject
{
    Q_OBJECT
public:
    explicit ModReferencePlayer(QObject* parent=nullptr);
    ~ModReferencePlayer() override;
    bool load(const QByteArray& data, QString* error=nullptr);
    void play(bool loop=true);
    void stop();
    bool isLoaded() const { return m_loaded; }
    QString title() const { return m_title; }

private:
    struct Sample { QByteArray pcm; int volume=64; int finetune=0; int loopStart=0; int loopLen=0; };
    struct Cell { int sample=0, period=0, effect=0, param=0; };
    struct Voice { int sample=0, period=0, basePeriod=0, targetPeriod=0, volume=64; double pos=0.0; double vibPhase=0.0; int effect=0,param=0; };
    class Device final : public QIODevice {
    public: explicit Device(ModReferencePlayer* p):QIODevice(p),m_p(p){}
        qint64 readData(char* d,qint64 n) override; qint64 writeData(const char*,qint64) override{return 0;}
        qint64 bytesAvailable() const override { return 8192+QIODevice::bytesAvailable(); }
    private: ModReferencePlayer* m_p=nullptr;
    };
    void render(qint16* out,int frames);
    void startRow(); void processTick(); void advanceRow();
    static quint16 be16(const QByteArray& d,int o);
    static double semitonePeriod(double p,int semi);

    QVector<Sample> m_samples; QVector<QVector<Cell>> m_patterns; QVector<int> m_order; Voice m_v[4];
    QString m_title; bool m_loaded=false,m_playing=false,m_loop=true; int m_orderPos=0,m_row=0,m_tick=0,m_speed=6,m_bpm=125;
    double m_samplesToTick=0.0; int m_rate=44100; QAudioSink* m_sink=nullptr; Device* m_dev=nullptr;
};
#endif
