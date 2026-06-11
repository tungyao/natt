#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QFrame>
#include <QTimer>
#include <QScrollBar>
#include <QDateTime>

#include "natcore/CoreClient.h"
#include <memory>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow()
        : m_startTime(0)
    {
        setWindowTitle("NATT - NAT Mesh Client");
        resize(960, 720);

        setupUI();

        m_client = std::make_shared<CoreClient>(std::move(m_cbs));

        m_pollTimer = new QTimer(this);
        connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onStatusPoll);
        m_pollTimer->start(1000);
    }

    ~MainWindow() = default;

private slots:
    void onStartClicked()
    {
        CoreConfig config;
        config.node_id            = m_nodeId->text().toStdString();
        config.network_id         = m_networkId->text().toStdString();
        config.control_url        = m_controlUrl->text().toStdString();
        config.stun_addr          = m_stunAddr->text().toStdString();
        config.udp_port           = static_cast<uint16_t>(m_udpPort->text().toUInt());
        config.connect_node_id    = m_connectNode->text().toStdString();
        config.local_addr         = m_relayAddr->text().toStdString();
        config.relay_addr         = m_relayAddr->text().toStdString();
        config.noise_private_key  = m_noiseKey->text().toStdString();
        config.use_ssl            = m_useWss->isChecked();
        config.cert_file          = m_certFile->text().toStdString();
        config.enable_tun         = m_enableTun->isChecked();

        appendLog("[info] Starting NATT client...");
        if (!m_client->start(config)) {
            appendLog("[error] CoreClient failed to start");
            return;
        }

        m_startTime = QDateTime::currentMSecsSinceEpoch();
        m_startBtn->setEnabled(false);
        m_stopBtn->setEnabled(true);
        setStateIndicator("connecting", "Connecting...");
    }

    void onStopClicked()
    {
        appendLog("[info] Stopping NATT client...");
        m_client->stop();
        m_startBtn->setEnabled(true);
        m_stopBtn->setEnabled(false);
        setStateIndicator("stopped", "Stopped");
        m_sMode->setText("—");
        m_sP2p->setText("—");
        m_sUptime->setText("—");
        m_peerList->clear();
        m_tunVip->setText("—");
        m_tunGw->setText("—");
        m_tunSubnet->setText("—");
        m_noTun->setVisible(true);
        m_startTime = 0;
    }

    void onStatusPoll()
    {
        if (!m_client->isRunning() && m_startTime > 0) {
            onStopClicked();
        }
        if (m_startTime > 0) {
            qint64 elapsed = (QDateTime::currentMSecsSinceEpoch() - m_startTime) / 1000;
            int m = static_cast<int>(elapsed / 60);
            int s = static_cast<int>(elapsed % 60);
            m_sUptime->setText(QString("%1m %2s").arg(m).arg(s));
        }
    }

private:
    void setupUI()
    {
        QWidget *central = new QWidget(this);
        setCentralWidget(central);
        QGridLayout *mainLayout = new QGridLayout(central);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(1);

        // ── Header ──────────────────────────────────────────
        QFrame *header = new QFrame;
        header->setObjectName("header");
        header->setFixedHeight(44);
        QHBoxLayout *hdrLayout = new QHBoxLayout(header);
        hdrLayout->setContentsMargins(12, 0, 16, 0);

        m_statusDot = new QLabel;
        m_statusDot->setFixedSize(10, 10);
        m_statusDot->setObjectName("statusDot");
        m_statusDot->setStyleSheet("background:#8888aa;border-radius:5px;");

        QLabel *title = new QLabel("NATT Client");
        title->setObjectName("title");

        m_statusLabel = new QLabel("Stopped");
        m_statusLabel->setObjectName("statusLabel");

        hdrLayout->addWidget(m_statusDot);
        hdrLayout->addSpacing(8);
        hdrLayout->addWidget(title);
        hdrLayout->addSpacing(12);
        hdrLayout->addWidget(m_statusLabel);
        hdrLayout->addStretch();

        mainLayout->addWidget(header, 0, 0, 1, 2);

        // ── Config Panel (top-left) ─────────────────────────
        QFrame *configPanel = new QFrame;
        configPanel->setObjectName("panel");
        QVBoxLayout *configLayout = new QVBoxLayout(configPanel);
        configLayout->setContentsMargins(14, 14, 14, 14);
        configLayout->setSpacing(8);

        QLabel *configTitle = new QLabel("CONFIGURATION");
        configTitle->setObjectName("panelTitle");
        configLayout->addWidget(configTitle);

        QFormLayout *form = new QFormLayout;
        form->setSpacing(6);
        form->setLabelAlignment(Qt::AlignRight);

        m_nodeId     = new QLineEdit("test-node");
        m_networkId  = new QLineEdit("home");
        m_controlUrl = new QLineEdit("127.0.0.1:8080");
        m_stunAddr   = new QLineEdit("127.0.0.1:3478");
        m_udpPort    = new QLineEdit("0");
        m_connectNode = new QLineEdit;
        m_relayAddr  = new QLineEdit;
        m_noiseKey   = new QLineEdit;
        m_certFile   = new QLineEdit;

        m_connectNode->setPlaceholderText("Peer node ID");
        m_relayAddr->setPlaceholderText("127.0.0.1:7000");
        m_noiseKey->setPlaceholderText("Base64 private key");
        m_certFile->setPlaceholderText("CA cert PEM path");

        form->addRow("Node ID",     m_nodeId);
        form->addRow("Network ID",  m_networkId);
        form->addRow("Control URL", m_controlUrl);
        form->addRow("STUN",        m_stunAddr);
        form->addRow("UDP Port",    m_udpPort);
        form->addRow("Connect",     m_connectNode);
        form->addRow("Relay",       m_relayAddr);
        form->addRow("Noise Key",   m_noiseKey);
        form->addRow("Cert File",   m_certFile);

        configLayout->addLayout(form);

        m_useWss   = new QCheckBox("Use WSS (TLS)");
        m_enableTun = new QCheckBox("Enable TUN interface");
        configLayout->addWidget(m_useWss);
        configLayout->addWidget(m_enableTun);

        QHBoxLayout *btnLayout = new QHBoxLayout;
        m_startBtn = new QPushButton("Start");
        m_startBtn->setObjectName("startBtn");
        m_stopBtn = new QPushButton("Stop");
        m_stopBtn->setObjectName("stopBtn");
        m_stopBtn->setEnabled(false);
        btnLayout->addWidget(m_startBtn);
        btnLayout->addWidget(m_stopBtn);
        configLayout->addLayout(btnLayout);

        configLayout->addStretch();

        mainLayout->addWidget(configPanel, 1, 0);

        // ── Status Panel (top-right) ────────────────────────
        QFrame *statusPanel = new QFrame;
        statusPanel->setObjectName("panel");
        QVBoxLayout *statusLayout = new QVBoxLayout(statusPanel);
        statusLayout->setContentsMargins(14, 14, 14, 14);
        statusLayout->setSpacing(8);

        QLabel *statusTitle = new QLabel("STATUS");
        statusTitle->setObjectName("panelTitle");
        statusLayout->addWidget(statusTitle);

        QGridLayout *statGrid = new QGridLayout;
        statGrid->setSpacing(4);

        auto addStat = [&](int row, int col, const QString& label, QLabel*& value) {
            QLabel *lbl = new QLabel(label);
            lbl->setObjectName("statLabel");
            value = new QLabel("—");
            value->setObjectName("statValue");
            statGrid->addWidget(lbl, row, col * 2);
            statGrid->addWidget(value, row, col * 2 + 1);
        };
        addStat(0, 0, "Mode", m_sMode);
        addStat(1, 0, "RTT", m_sRtt);
        addStat(0, 1, "P2P Status", m_sP2p);
        addStat(1, 1, "Uptime", m_sUptime);

        statusLayout->addLayout(statGrid);

        QLabel *peerTitle = new QLabel("Online Peers");
        peerTitle->setObjectName("statLabel");
        statusLayout->addWidget(peerTitle);

        m_peerList = new QListWidget;
        m_peerList->setObjectName("peerList");
        m_peerList->addItem("No peers connected");
        statusLayout->addWidget(m_peerList, 1);

        mainLayout->addWidget(statusPanel, 1, 1);

        // ── TUN Panel (bottom-left) ─────────────────────────
        QFrame *tunPanel = new QFrame;
        tunPanel->setObjectName("panel");
        QVBoxLayout *tunLayout = new QVBoxLayout(tunPanel);
        tunLayout->setContentsMargins(14, 14, 14, 14);
        tunLayout->setSpacing(8);

        QLabel *tunTitle = new QLabel("TUN INTERFACE");
        tunTitle->setObjectName("panelTitle");
        tunLayout->addWidget(tunTitle);

        m_noTun = new QWidget;
        QVBoxLayout *noTunLayout = new QVBoxLayout(m_noTun);
        QLabel *noTunLabel = new QLabel("No TUN interface active");
        noTunLabel->setObjectName("noTunLabel");
        noTunLayout->addWidget(noTunLabel);
        tunLayout->addWidget(m_noTun);

        QGridLayout *tunGrid = new QGridLayout;
        tunGrid->setSpacing(3);
        auto addTun = [&](int row, const QString& label, QLabel*& value) {
            QLabel *lbl = new QLabel(label);
            lbl->setObjectName("tunLabel");
            value = new QLabel("—");
            value->setObjectName("tunValue");
            tunGrid->addWidget(lbl, row, 0);
            tunGrid->addWidget(value, row, 1);
        };
        addTun(0, "Virtual IP",  m_tunVip);
        addTun(1, "Gateway",     m_tunGw);
        addTun(2, "Subnet",      m_tunSubnet);
        tunLayout->addLayout(tunGrid);
        tunLayout->addStretch();

        mainLayout->addWidget(tunPanel, 2, 0);

        // ── Log Panel (bottom-right) ────────────────────────
        QFrame *logPanel = new QFrame;
        logPanel->setObjectName("panel");
        QVBoxLayout *logLayout = new QVBoxLayout(logPanel);
        logLayout->setContentsMargins(14, 14, 14, 14);
        logLayout->setSpacing(8);

        QLabel *logTitle = new QLabel("LOG");
        logTitle->setObjectName("panelTitle");
        logLayout->addWidget(logTitle);

        m_logView = new QPlainTextEdit;
        m_logView->setReadOnly(true);
        m_logView->setObjectName("logView");
        logLayout->addWidget(m_logView, 1);

        mainLayout->addWidget(logPanel, 2, 1);

        // ── Row/column stretch ─────────────────────────────
        mainLayout->setRowStretch(0, 0);
        mainLayout->setRowStretch(1, 1);
        mainLayout->setRowStretch(2, 2);
        mainLayout->setColumnStretch(0, 1);
        mainLayout->setColumnStretch(1, 1);

        // ── Stylesheet ──────────────────────────────────────
        setStyleSheet(R"(
            QMainWindow, QWidget { background: #1a1b2e; color: #e0e0f0; font-size: 13px; font-family: 'Segoe UI', sans-serif; }
            QFrame#header { background: #242540; border-bottom: 1px solid #2d2f54; }
            QLabel#title { font-size: 15px; font-weight: 600; }
            QLabel#statusLabel { font-size: 12px; color: #8888aa; }
            QFrame#panel { background: #242540; border: none; }
            QLabel#panelTitle { font-size: 11px; text-transform: uppercase; letter-spacing: 1px; color: #8888aa; }
            QLabel#statLabel { font-size: 10px; color: #8888aa; text-transform: uppercase; }
            QLabel#statValue { font-size: 14px; font-weight: 500; font-family: 'Cascadia Code', 'Fira Code', monospace; }
            QLabel#tunLabel { font-size: 11px; color: #8888aa; }
            QLabel#tunValue { font-size: 13px; font-family: monospace; }
            QLabel#noTunLabel { color: #8888aa; font-size: 12px; font-style: italic; }
            QLineEdit { padding: 5px 8px; border: 1px solid #2d2f54; border-radius: 4px; background: #1a1b2e; color: #e0e0f0; font-size: 12px; }
            QLineEdit:focus { border-color: #7c5cfc; }
            QCheckBox { font-size: 12px; spacing: 6px; }
            QCheckBox::indicator { width: 16px; height: 16px; }
            QPushButton { padding: 6px 16px; border: none; border-radius: 4px; font-size: 12px; font-weight: 500; }
            QPushButton#startBtn { background: #7c5cfc; color: #fff; }
            QPushButton#startBtn:hover { background: #6a4ae8; }
            QPushButton#startBtn:disabled { background: #3a3a5c; color: #666; }
            QPushButton#stopBtn { background: #f87171; color: #fff; }
            QPushButton#stopBtn:hover { background: #e06060; }
            QPushButton#stopBtn:disabled { background: #3a3a5c; color: #666; }
            QListWidget#peerList { background: #1a1b2e; border: none; border-radius: 3px; font-size: 12px; font-family: monospace; }
            QListWidget#peerList::item { padding: 3px 6px; }
            QPlainTextEdit#logView { background: #1a1b2e; border: none; border-radius: 4px; font-family: 'Cascadia Code', 'Fira Code', monospace; font-size: 11px; color: #e0e0f0; }
        )");

        // ── Callbacks ──────────────────────────────────────
        m_cbs.on_log = [this](const std::string& line) {
            QMetaObject::invokeMethod(this, [this, line]() {
                appendLog(QString::fromStdString(line));
            });
        };

        m_cbs.on_state_change = [this](const std::string& state) {
            QMetaObject::invokeMethod(this, [this, state]() {
                setState(QString::fromStdString(state));
            });
        };

        m_cbs.on_peer_online = [this](const std::string& node_id,
                                      const std::string& ip, uint16_t port) {
            QMetaObject::invokeMethod(this, [this, node_id, ip, port]() {
                addPeer(QString::fromStdString(node_id),
                        QString::fromStdString(ip), port);
            });
        };

        m_cbs.on_punch_success = [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                setPunchSuccess();
            });
        };

        m_cbs.on_error = [this](const std::string& msg) {
            QMetaObject::invokeMethod(this, [this, msg]() {
                setError(QString::fromStdString(msg));
            });
        };

        m_cbs.on_virtual_ip_assigned = [this](const std::string& vip,
                                              const std::string& gw,
                                              const std::string& subnet) {
            QMetaObject::invokeMethod(this, [this, vip, gw, subnet]() {
                setTunInfo(QString::fromStdString(vip),
                           QString::fromStdString(gw),
                           QString::fromStdString(subnet));
            });
        };

        // ── Button signals ─────────────────────────────────
        connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
        connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    }

    void appendLog(const QString& line)
    {
        QString color;
        if (line.contains("[error]") || line.contains("[critical]"))
            color = "#f87171";
        else if (line.contains("[warn]"))
            color = "#fbbf24";
        else
            color = "#e0e0f0";

        m_logView->appendHtml(QString("<span style=\"color:%1\">%2</span>").arg(color, line.toHtmlEscaped()));

        // Auto-scroll
        QScrollBar *sb = m_logView->verticalScrollBar();
        sb->setValue(sb->maximum());
    }

    void setState(const QString& state)
    {
        if (state == "p2p" || state == "relay") {
            setPunchSuccess();
        }
    }

    void setPunchSuccess()
    {
        m_sP2p->setText("✓ Connected");
    }

    void setError(const QString& msg)
    {
        appendLog("[error] " + msg);
        setStateIndicator("error", "Failed");
    }

    void addPeer(const QString& nodeId, const QString& ip, uint16_t port)
    {
        if (m_peerList->count() == 1 && m_peerList->item(0)->text() == "No peers connected") {
            m_peerList->clear();
        }
        QString text = nodeId;
        if (!ip.isEmpty()) {
            text += " @ " + ip + ":" + QString::number(port);
        }
        m_peerList->addItem(text);
    }

    void setTunInfo(const QString& vip, const QString& gw, const QString& subnet)
    {
        m_noTun->setVisible(false);
        m_tunVip->setText(vip);
        m_tunGw->setText(gw.isEmpty() ? "—" : gw);
        m_tunSubnet->setText(subnet.isEmpty() ? "—" : subnet);
    }

    void setStateIndicator(const QString& state, const QString& label)
    {
        m_statusLabel->setText(label);
        QString color;
        if (state == "connected" || state == "p2p" || state == "relay") {
            color = "#4ade80";
        } else if (state == "connecting") {
            color = "#fbbf24";
        } else if (state == "error" || state == "failed") {
            color = "#f87171";
        } else {
            color = "#8888aa";
        }
        m_statusDot->setStyleSheet(
            QString("background:%1;border-radius:5px;").arg(color));

        m_sMode->setText(state);
    }

    // Config inputs
    QLineEdit *m_nodeId, *m_networkId, *m_controlUrl, *m_stunAddr;
    QLineEdit *m_udpPort, *m_connectNode, *m_relayAddr;
    QLineEdit *m_noiseKey, *m_certFile;
    QCheckBox *m_useWss, *m_enableTun;
    QPushButton *m_startBtn, *m_stopBtn;

    // Status
    QLabel *m_statusDot, *m_statusLabel;
    QLabel *m_sMode, *m_sRtt, *m_sP2p, *m_sUptime;
    QListWidget *m_peerList;

    // TUN
    QLabel *m_tunVip, *m_tunGw, *m_tunSubnet;
    QWidget *m_noTun;

    // Log
    QPlainTextEdit *m_logView;

    // Core
    std::shared_ptr<CoreClient> m_client;
    CoreCallbacks m_cbs;
    QTimer *m_pollTimer;
    qint64 m_startTime;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}

#include "main.moc"
