#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QAction>
#include <QCloseEvent>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QDBusMessage>
#include <QUrl>
#include <QVariantMap>
#include <QDateTime>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QVector>
#include <QWheelEvent>
#include <QWidget>

class Canvas : public QWidget {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.screenshock.App")
public:
    void setDaemon(bool d) { daemon_ = d; }

    Canvas() {
        setMinimumSize(400, 300);
        setFocusPolicy(Qt::StrongFocus);
        updateTitle();
    }

    // Hide the window, wait a moment, take a screenshot, show the preview.
    void capture() {
        hide();
        QTimer::singleShot(350, this, [this] {
            // X11: direct grab is instant.
            if (QGuiApplication::platformName() == "xcb") {
                QPixmap shot = QGuiApplication::primaryScreen()->grabWindow(0);
                if (!shot.isNull()) { showImage(shot); return; }
            }
            // Wayland: ask the desktop portal.
            captureViaPortal();
        });
    }

public slots:
    Q_SCRIPTABLE void Capture() { capture(); } // callable over D-Bus

    void onPortalResponse(uint code, const QVariantMap &results) {
        if (code != 0) { fail("Screenshot was cancelled or denied."); return; }
        QString file = QUrl(results.value("uri").toString()).toLocalFile();
        QPixmap shot(file);
        if (shot.isNull()) { fail("Could not load screenshot: " + file); return; }
        showImage(shot);
    }

private:
    void captureViaPortal() {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) { fail("No D-Bus session bus available."); return; }

        // The portal replies on a request object whose path we can predict,
        // so subscribe BEFORE calling to avoid missing the signal.
        QString token = "screenshock" + QString::number(QDateTime::currentMSecsSinceEpoch());
        QString sender = bus.baseService().mid(1).replace('.', '_');
        QString path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
        bus.connect("org.freedesktop.portal.Desktop", path,
                    "org.freedesktop.portal.Request", "Response",
                    this, SLOT(onPortalResponse(uint, QVariantMap)));

        QDBusMessage msg = QDBusMessage::createMethodCall(
            "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.Screenshot", "Screenshot");
        QVariantMap opts;
        opts["handle_token"] = token;
        opts["interactive"] = false;
        msg << QString("") << opts;

        QDBusMessage reply = bus.call(msg);
        if (reply.type() == QDBusMessage::ErrorMessage)
            fail("Portal error: " + reply.errorMessage());
    }

    void showImage(const QPixmap &shot) {
        image_ = shot;
        image_.setDevicePixelRatio(1.0);
        undo_.clear();
        QScreen *screen = QGuiApplication::primaryScreen();
        QSize avail = screen->availableGeometry().size() * 0.9;
        resize(image_.size().scaled(avail, Qt::KeepAspectRatio));
        show();
        raise();
        activateWindow();
        update();
    }

    void fail(const QString &why) {
        show();
        QMessageBox::warning(this, "screenshock", why);
        if (image_.isNull()) {
            if (daemon_) hide(); else QApplication::quit();
        }
    }

protected:
    void closeEvent(QCloseEvent *e) override {
        if (daemon_) { image_ = QPixmap(); undo_.clear(); } // free memory, keep running
        e->accept();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(30, 30, 30));
        if (image_.isNull()) return;
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(targetRect(), image_, image_.rect());
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (drawMode_ && e->button() == Qt::LeftButton && !image_.isNull()) {
            undo_.push_back(image_);
            if (undo_.size() > 30) undo_.removeFirst();
            drawing_ = true;
            last_ = toImage(e->pos());
            drawLine(last_, last_); // allows single-dot clicks
            update();
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (drawing_) {
            QPointF cur = toImage(e->pos());
            drawLine(last_, cur);
            last_ = cur;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) drawing_ = false;
    }

    void wheelEvent(QWheelEvent *e) override {
        pen_ = qBound(1, pen_ + (e->angleDelta().y() > 0 ? 1 : -1), 60);
        updateTitle();
    }

    void keyPressEvent(QKeyEvent *e) override {
        const bool ctrl = e->modifiers() & Qt::ControlModifier;
        const bool shift = e->modifiers() & Qt::ShiftModifier;

        if (e->key() == Qt::Key_Escape) {
            close();
        } else if (ctrl && shift && e->key() == Qt::Key_C) {
            QApplication::clipboard()->setPixmap(image_);
        } else if (ctrl && e->key() == Qt::Key_C) {
            QColor c = QColorDialog::getColor(color_, this, "Choose pen color");
            if (c.isValid()) color_ = c;
            updateTitle();
        } else if (ctrl && e->key() == Qt::Key_D) {
            drawMode_ = !drawMode_;
            setCursor(drawMode_ ? Qt::CrossCursor : Qt::ArrowCursor);
            updateTitle();
        } else if (ctrl && e->key() == Qt::Key_Z) {
            if (!undo_.isEmpty()) {
                image_ = undo_.takeLast();
                update();
            }
        } else if (ctrl && e->key() == Qt::Key_S) {
            save();
        } else if (ctrl && e->key() == Qt::Key_N) {
            capture();
        } else {
            QWidget::keyPressEvent(e);
        }
    }

private:
    QRectF targetRect() const {
        QSizeF s = QSizeF(image_.size()).scaled(size(), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    }

    QPointF toImage(const QPoint &p) const {
        QRectF t = targetRect();
        double scale = image_.width() / t.width();
        return QPointF((p.x() - t.left()) * scale, (p.y() - t.top()) * scale);
    }

    void drawLine(const QPointF &a, const QPointF &b) {
        QPainter p(&image_);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color_, pen_, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(a, b);
    }

    void save() {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        QString name = dir + "/screenshot_" +
        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
        QString path = QFileDialog::getSaveFileName(this, "Save screenshot", name,
                                                    "PNG (*.png);;JPEG (*.jpg)");
        if (!path.isEmpty()) image_.save(path);
    }

    void updateTitle() {
        setWindowTitle(QString("screenshock | Draw: %1 | Pen: %2px | Color: %3 | "
        "Ctrl+D draw, Ctrl+C color, Ctrl+Z undo, Ctrl+S save")
        .arg(drawMode_ ? "ON (left-click drag)" : "OFF")
        .arg(pen_)
        .arg(color_.name()));
    }

    QPixmap image_;
    QVector<QPixmap> undo_;
    QColor color_{Qt::red};
    int pen_ = 4;
    bool drawMode_ = false;
    bool drawing_ = false;
    bool daemon_ = false;
    QPointF last_;
};

static const char *kService = "org.screenshock.App";
static const char *kPath = "/App";

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("screenshock");
    QGuiApplication::setDesktopFileName("screenshock"); // Wayland app_id / window class

    const bool daemonMode = app.arguments().contains("--daemon");
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Normal call: if a daemon is already running, just tell it to capture.
    if (!daemonMode && bus.isConnected() && bus.interface() &&
        bus.interface()->isServiceRegistered(kService)) {
        bus.call(QDBusMessage::createMethodCall(kService, kPath, kService, "Capture"));
    return 0;
        }

        Canvas w;

        if (daemonMode) {
            if (!bus.registerService(kService)) {
                qWarning("screenshock daemon is already running.");
                return 1;
            }
            w.setDaemon(true);
            bus.registerObject(kPath, &w, QDBusConnection::ExportScriptableSlots);
            QApplication::setQuitOnLastWindowClosed(false);

            // Optional tray icon (some desktops, e.g. GNOME, need an extension to show it).
            QIcon icon = QIcon::fromTheme("applets-screenshooter",
                                          QIcon::fromTheme("camera-photo"));
            if (icon.isNull()) { // no icon theme installed: draw a simple fallback
                QPixmap pm(64, 64);
                pm.fill(Qt::transparent);
                QPainter p(&pm);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(220, 50, 50));
                p.drawEllipse(8, 8, 48, 48);
                p.end();
                icon = QIcon(pm);
            }
            QSystemTrayIcon tray(icon);
            QMenu menu;
            QObject::connect(menu.addAction("Take screenshot"), &QAction::triggered,
                             [&w] { w.capture(); });
            QObject::connect(menu.addAction("Quit"), &QAction::triggered,
                             &app, &QApplication::quit);
            tray.setContextMenu(&menu);
            QObject::connect(&tray, &QSystemTrayIcon::activated,
                             [&w](QSystemTrayIcon::ActivationReason r) {
                                 if (r == QSystemTrayIcon::Trigger) w.capture();
                             });
                                 tray.show();

                                 return app.exec();
        }

        w.capture();
        return app.exec();
}

#include "main.moc"
