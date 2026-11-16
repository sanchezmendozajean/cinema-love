#pragma once

#include <QJsonObject>
#include <QQuickFramebufferObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

struct mpv_handle;
struct mpv_render_context;

/**
 * Elemento de Qt Quick que reproduce con libmpv. mpv dibuja en un FBO (API de render OpenGL), de modo que los
 * controles QML se pintan encima como cualquier otro Item. Ver specs/02-reproductor-mpv.md.
 */
class MpvItem : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(QString audioId READ audioId NOTIFY tracksChanged)
    Q_PROPERTY(QString subtitleId READ subtitleId NOTIFY tracksChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString info READ info NOTIFY infoChanged)

public:
    explicit MpvItem(QQuickItem *parent = nullptr);
    ~MpvItem() override;

    Renderer *createRenderer() const override;

    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }
    bool buffering() const { return m_buffering; }
    double volume() const { return m_volume; }
    QVariantList audioTracks() const { return m_audio; }
    QVariantList subtitleTracks() const { return m_subs; }
    QString audioId() const { return m_aid; }
    QString subtitleId() const { return m_sid; }
    QString state() const { return m_state; }
    QString info() const { return m_info; }

    void setVolume(double v);

    Q_INVOKABLE void load(const QString &source, double start = 0);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds, bool relative);
    Q_INVOKABLE void setAudio(const QString &id);
    Q_INVOKABLE void setSubtitle(const QString &id);
    Q_INVOKABLE void addSubtitle(const QString &path);
    Q_INVOKABLE void stop();
    /** Destrucción ordenada de mpv con la ventana y el bucle de eventos aún vivos (llamar antes de cerrar). */
    Q_INVOKABLE void shutdown();
    /** Captura la ventana completa (video de mpv + controles QML) a un PNG. */
    Q_INVOKABLE bool grabWindowTo(const QString &path);
    /** Instantánea de métricas (decodificación por hardware, fotogramas perdidos, códecs…) para el spike. */
    Q_INVOKABLE QVariantMap stats() const;
    Q_INVOKABLE bool writeStats(const QString &path, const QVariantMap &extra = {}) const;

signals:
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void bufferingChanged();
    void volumeChanged();
    void tracksChanged();
    void stateChanged();
    void infoChanged();
    void endOfFile(const QString &reason);

private slots:
    void processEvents();
    void doUpdate();
    /** Hilo de render, con el contexto GL activo: único sitio seguro para liberar el contexto de render de mpv. */
    void releaseRender();

private:
    friend class MpvRenderer;

    void setState(const QString &s);
    void refreshTracks();
    void refreshInfo();
    QString getString(const char *name) const;
    void setOption(const char *name, const char *value);
    void command(const QStringList &args);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_gl = nullptr;

    double m_position = 0;
    double m_duration = 0;
    double m_volume = 100;
    bool m_paused = false;
    bool m_buffering = false;
    QVariantList m_audio;
    QVariantList m_subs;
    QString m_aid = QStringLiteral("auto");
    QString m_sid = QStringLiteral("auto");
    QString m_state = QStringLiteral("inactivo");
    QString m_info;
};
