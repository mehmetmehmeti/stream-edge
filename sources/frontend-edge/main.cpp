#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QCoreApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <qqml.h>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QElapsedTimer>
#include <QDateTime>
#include <memory>

#include "VideoPreviewItem.h"

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QCoreApplication::setOrganizationName(QStringLiteral("Diploma"));
    QCoreApplication::setApplicationName(QStringLiteral("DiplomaFrontendEdge"));

    QGuiApplication app(argc, argv);
    qmlRegisterType<VideoPreviewItem>("DiplomaFrontendEdge", 1, 0, "VideoPreviewItem");

    QQmlApplicationEngine engine;

    const QUrl url(QStringLiteral("qrc:/DiplomaFrontendEdge/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(url);
    if (app.arguments().contains("--benchmark-camera") && !engine.rootObjects().isEmpty())
    {
        auto* root = engine.rootObjects().first();
        root->setProperty("apiBaseUrl", qEnvironmentVariable("DIPLOMA_TEST_API_URL", "http://127.0.0.1:3002"));
        if (qEnvironmentVariableIsSet("DIPLOMA_TEST_AUTH_TOKEN"))
            root->setProperty("token", qEnvironmentVariable("DIPLOMA_TEST_AUTH_TOKEN"));
        QMetaObject::invokeMethod(root, "refreshAll", Q_ARG(QVariant, true));
        auto output = std::make_shared<QFile>(qEnvironmentVariable("DIPLOMA_TEST_OUTPUT", "/tmp/diploma-qt-benchmark.jsonl"));
        if (!output->open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
        auto elapsed = std::make_shared<QElapsedTimer>();
        elapsed->start();
        auto* timer = new QTimer(root);
        timer->setInterval(1000);
        QObject::connect(timer, &QTimer::timeout, root, [root, output, elapsed]() {
            auto* preview = root->findChild<VideoPreviewItem*>("cameraPreview");
            QJsonObject sample{{"elapsed_ns", static_cast<double>(elapsed->nsecsElapsed())},
                               {"utc_ms", static_cast<double>(QDateTime::currentMSecsSinceEpoch())},
                               {"preview_present", preview != nullptr}};
            if (preview) {
                sample["rendered_frames"] = static_cast<double>(preview->renderedFrameCount());
                sample["published_frames"] = static_cast<double>(preview->publishedFrameCount());
                sample["acquired_frames"] = static_cast<double>(preview->acquiredFrameCount());
                sample["bridge_dropped_frames"] = static_cast<double>(preview->droppedFrameCount());
                sample["missing_frame_polls"] = static_cast<double>(preview->missingFrameCount());
                sample["invalid_frames"] = static_cast<double>(preview->invalidFrameCount());
                sample["interop_failures"] = static_cast<double>(preview->interopFailureCount());
                sample["live"] = preview->liveFrameActive();
            }
            output->write(QJsonDocument(sample).toJson(QJsonDocument::Compact) + '\n');
            output->flush();
        });
        timer->start();
        QTimer::singleShot(12000, root, [root, output]() {
            auto* window = qobject_cast<QQuickWindow*>(root);
            if (window)
                window->grabWindow().save(QFileInfo(output->fileName()).absolutePath() + "/qt-live.png");
        });
    }
    if (app.arguments().contains("--verify-recording") && !engine.rootObjects().isEmpty())
    {
        auto* root = engine.rootObjects().first();
        auto report = std::make_shared<QJsonObject>();
        auto pausedPosition = std::make_shared<qint64>(0);
        auto framesBeforeResume = std::make_shared<qulonglong>(0);
        const auto output = qEnvironmentVariable("DIPLOMA_TEST_OUTPUT", "/tmp/diploma-qt-recording");
        const auto screenshot = [root, output](const QString& suffix) {
            auto* window = qobject_cast<QQuickWindow*>(root);
            if (!window) return;
            const auto image = window->grabWindow().convertToFormat(QImage::Format_RGB888);
            QFile file(output + suffix + ".ppm");
            if (!file.open(QIODevice::WriteOnly)) return;
            file.write("P6\n" + QByteArray::number(image.width()) + " " + QByteArray::number(image.height()) + "\n255\n");
            for (int row = 0; row < image.height(); ++row)
                file.write(reinterpret_cast<const char*>(image.constScanLine(row)), image.width() * 3);
        };
        QTimer::singleShot(2000, root, [root, report]() {
            const auto value = root->property("videos");
            const auto videos = value.value<QJSValue>().toVariant().toList();
            for (const auto& entry : videos) {
                auto video = entry.toMap();
                const auto requested = qEnvironmentVariable("DIPLOMA_TEST_RECORDING_ID");
                if (video.value("status") != "completed" ||
                    (!requested.isEmpty() && video.value("id") != requested)) continue;
                if (qEnvironmentVariableIsSet("DIPLOMA_TEST_REMOTE")) video["edge_local_path"] = "";
                root->setProperty("workspaceIndex", 1);
                QMetaObject::invokeMethod(root, "openRecording", Q_ARG(QVariant, video));
                (*report)["videoId"] = video.value("id").toString();
                return;
            }
            (*report)["selectionFailed"] = true;
        });
        QTimer::singleShot(6000, root, [root, report, pausedPosition, screenshot]() {
            auto* preview = root->findChild<VideoPreviewItem*>("recordingPreview");
            (*report)["decoded"] = preview && preview->renderedFrameCount() > 10 && preview->durationMs() > 0;
            if (!preview) return;
            (*report)["durationMs"] = preview->durationMs();
            (*report)["initialFrames"] = static_cast<double>(preview->renderedFrameCount());
            (*report)["initialPositionMs"] = preview->positionMs();
            preview->setPaused(true);
            *pausedPosition = preview->positionMs();
            screenshot("-windowed");
        });
        QTimer::singleShot(7000, root, [root, report, pausedPosition]() {
            auto* preview = root->findChild<VideoPreviewItem*>("recordingPreview");
            if (!preview) return;
            (*report)["pauseWorks"] = qAbs(preview->positionMs() - *pausedPosition) < 400;
            preview->seek(preview->durationMs() / 2);
        });
        QTimer::singleShot(8500, root, [root, report, framesBeforeResume]() {
            auto* preview = root->findChild<VideoPreviewItem*>("recordingPreview");
            if (!preview) return;
            (*report)["seekWorks"] = qAbs(preview->positionMs() - preview->durationMs() / 2) < 1200;
            *framesBeforeResume = preview->renderedFrameCount();
            if (auto* button = root->findChild<QObject*>("recordingFullscreenButton"))
                QMetaObject::invokeMethod(button, "clicked");
        });
        QTimer::singleShot(10000, root, [root, report, screenshot]() {
            auto* preview = root->findChild<VideoPreviewItem*>("recordingPreview");
            (*report)["fullscreenPositionMs"] = preview ? preview->positionMs() : -1;
            (*report)["fullscreenVisibility"] = qobject_cast<QQuickWindow*>(root)->visibility();
            (*report)["fullscreenWorks"] = root->property("fullscreenTarget") == "recording" &&
                qobject_cast<QQuickWindow*>(root)->visibility() == QWindow::FullScreen && preview &&
                qAbs(preview->positionMs() - preview->durationMs() / 2) < 1200;
            screenshot("-fullscreen");
            if (auto* button = root->findChild<QObject*>("recordingPlayButton"))
                QMetaObject::invokeMethod(button, "clicked");
        });
        QTimer::singleShot(12000, root, [root, report, framesBeforeResume]() {
            auto* preview = root->findChild<VideoPreviewItem*>("recordingPreview");
            (*report)["resumeWorks"] = preview && preview->renderedFrameCount() > *framesBeforeResume + 10;
            if (auto* button = root->findChild<QObject*>("recordingFullscreenButton"))
                QMetaObject::invokeMethod(button, "clicked");
            if (preview) preview->seek(preview->durationMs() - 200);
        });
        QTimer::singleShot(14000, root, [root, report]() {
            auto* preview = root->findChild<VideoPreviewItem*>("recordingPreview");
            (*report)["exitWorks"] = root->property("fullscreenTarget").toString().isEmpty();
            (*report)["endWorks"] = preview && preview->playbackFinished();
            if (auto* button = root->findChild<QObject*>("recordingPlayButton"))
                QMetaObject::invokeMethod(button, "clicked");
        });
        QTimer::singleShot(16000, root, [root, report, output]() {
            auto* preview = root->findChild<VideoPreviewItem*>("recordingPreview");
            (*report)["replayWorks"] = preview && !preview->playbackFinished() && preview->positionMs() < 3500;
            (*report)["replayPositionMs"] = preview ? preview->positionMs() : -1;
            (*report)["error"] = preview ? preview->playbackError() : "Player missing";
            QFile file(output + ".json");
            if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(*report).toJson());
            bool passed = report->value("error").toString().isEmpty();
            for (const auto* key : {"decoded", "pauseWorks", "seekWorks", "fullscreenWorks", "resumeWorks", "exitWorks", "endWorks", "replayWorks"})
                passed = passed && report->value(key).toBool();
            QCoreApplication::exit(passed ? 0 : 1);
        });
    }
    if (app.arguments().contains("--verify-camera") && !engine.rootObjects().isEmpty())
    {
        auto* root = engine.rootObjects().first();
        if (auto* window = qobject_cast<QQuickWindow*>(root)) {
            const int width = qEnvironmentVariableIntValue("DIPLOMA_TEST_WIDTH");
            const int height = qEnvironmentVariableIntValue("DIPLOMA_TEST_HEIGHT");
            if (width > 0 && height > 0) {
                QTimer::singleShot(13000, window, [window, width, height]() {
                    window->showNormal();
                    window->resize(width, height);
                });
            }
        }
        if (qEnvironmentVariableIsSet("DIPLOMA_TEST_AUTH_TOKEN"))
            root->setProperty("token", qEnvironmentVariable("DIPLOMA_TEST_AUTH_TOKEN"));
        QMetaObject::invokeMethod(root, "refreshAll", Q_ARG(QVariant, true));
        if (qEnvironmentVariableIsSet("DIPLOMA_TEST_START"))
            QTimer::singleShot(1500, root, [root]() { QMetaObject::invokeMethod(root, "startCurrentStream"); });
        if (qEnvironmentVariableIsSet("DIPLOMA_TEST_FULLSCREEN"))
            QTimer::singleShot(12000, root, [root]() {
                if (auto* button = root->findChild<QObject*>("liveFullscreenButton"))
                    QMetaObject::invokeMethod(button, "clicked");
            });
        QTimer::singleShot(15000, root, [root]() {
            auto* preview = root->findChild<VideoPreviewItem*>("cameraPreview");
            const auto frames = preview ? preview->renderedFrameCount() : 0;
            const bool live = preview && preview->liveFrameActive() && frames > 30;
            const auto output = qEnvironmentVariable("DIPLOMA_TEST_OUTPUT", "/tmp/diploma-qt-camera");
            auto* window = qobject_cast<QQuickWindow*>(root);
            if (window) {
                const auto image = window->grabWindow().convertToFormat(QImage::Format_RGB888);
                QFile screenshot(output + ".ppm");
                if (screenshot.open(QIODevice::WriteOnly)) {
                    screenshot.write("P6\n" + QByteArray::number(image.width()) + " " + QByteArray::number(image.height()) + "\n255\n");
                    for (int row = 0; row < image.height(); ++row)
                        screenshot.write(reinterpret_cast<const char*>(image.constScanLine(row)), image.width() * 3);
                }
            }
            QFile report(output + ".json");
            const bool fullscreen = !qEnvironmentVariableIsSet("DIPLOMA_TEST_FULLSCREEN") ||
                (root->property("fullscreenTarget") == "live" && window &&
                 window->visibility() == QWindow::FullScreen && preview &&
                 preview->width() >= window->width() - 1 && preview->height() >= window->height() - 1);
            if (report.open(QIODevice::WriteOnly))
                report.write(QJsonDocument(QJsonObject{{"live", live}, {"fullscreen", fullscreen},
                    {"renderedFrames", static_cast<double>(frames)}}).toJson());
            QCoreApplication::exit(live && fullscreen ? 0 : 1);
        });
    }
    return app.exec();
}
