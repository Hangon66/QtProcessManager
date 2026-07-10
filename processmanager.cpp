#include "processmanager.h"
#include <QDebug>
#include <QRegularExpression>
#include <QFileInfo>

/**
 * @brief 构造函数，创建 QProcess 并连接内部信号
 */
ProcessManager::ProcessManager(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_killTimer(new QTimer(this))
{
    // 定时器单次触发，超时后强制 kill
    m_killTimer->setSingleShot(true);

    // 连接 QProcess 信号到 ProcessManager 对外信号
    connect(m_process, &QProcess::started, this, &ProcessManager::started);

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        // 停止 kill 定时器（若进程已正常退出则不再需要）
        m_killTimer->stop();
        emit finished(exitCode, exitStatus);
    });

    // 解码辅助 lambda：优先 UTF-8，失败则回退本地编码（GBK）
    auto decodeOutput = [](const QByteArray &data) -> QString {
        if (data.isEmpty()) return {};
        QString utf8 = QString::fromUtf8(data);
        if (!utf8.contains(QChar::ReplacementCharacter))
            return utf8;
        return QString::fromLocal8Bit(data);
    };

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this, decodeOutput]() {
        QString text = decodeOutput(m_process->readAllStandardOutput());
        const QStringList lines = text.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            qDebug() << "[ProcessManager][stdout]" << line;
            emit outputReceived(line);
        }
    });

    connect(m_process, &QProcess::readyReadStandardError, this, [this, decodeOutput]() {
        QString text = decodeOutput(m_process->readAllStandardError());
        const QStringList lines = text.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            qWarning() << "[ProcessManager][stderr]" << line;
            emit errorReceived(line);
        }
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // 发生错误时也停止 kill 定时器
        m_killTimer->stop();
        qWarning() << "[ProcessManager] 进程错误:" << error << m_process->errorString();
        emit errorOccurred(error);
    });

    // 超时后强制终止整个进程树
    connect(m_killTimer, &QTimer::timeout, this, [this]() {
        if (m_process->state() != QProcess::NotRunning) {
            qWarning() << "[ProcessManager] terminate 超时，强制 kill 进程树";
            killProcessTree();
        }
    });
}

/**
 * @brief 析构函数，确保子进程树已终止
 */
ProcessManager::~ProcessManager()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            killProcessTree();
            m_process->waitForFinished(1000);
        }
    }
}

/**
 * @brief 设置要执行的命令（程序路径 + 参数列表）
 */
void ProcessManager::setCommand(const QString &program, const QStringList &arguments)
{
    m_program = program;
    m_arguments = arguments;
}

/**
 * @brief 设置工作目录
 */
void ProcessManager::setWorkingDirectory(const QString &dir)
{
    m_workingDirectory = dir;
}

/**
 * @brief 启动进程（直接执行模式）
 */
void ProcessManager::start()
{
    if (isRunning()) {
        qWarning() << "[ProcessManager] 进程已在运行，忽略重复启动请求";
        return;
    }

    if (m_program.isEmpty()) {
        qWarning() << "[ProcessManager] 程序路径为空，请先调用 setCommand()";
        return;
    }

    if (!m_workingDirectory.isEmpty()) {
        m_process->setWorkingDirectory(m_workingDirectory);
    }

    qDebug() << "[ProcessManager] 启动进程:" << m_program << m_arguments;
    m_process->start(m_program, m_arguments);
}

/**
 * @brief 通过 cmd.exe /c 执行完整命令行（Shell 模式）
 */
void ProcessManager::executeShell(const QString &commandLine)
{
    if (isRunning()) {
        qWarning() << "[ProcessManager] 进程已在运行，忽略重复启动请求";
        return;
    }

    if (commandLine.isEmpty()) {
        qWarning() << "[ProcessManager] 命令行为空，无法执行";
        return;
    }

    if (!m_workingDirectory.isEmpty()) {
        m_process->setWorkingDirectory(m_workingDirectory);
    }

    qDebug() << "[ProcessManager] Shell 执行:" << commandLine;
    m_process->start("cmd.exe", QStringList() << "/c" << commandLine);
}

/**
 * @brief 优雅终止进程：先 terminate，超时后 kill
 */
void ProcessManager::stop(int killTimeoutMs)
{
    if (!isRunning()) {
        return;
    }

    qDebug() << "[ProcessManager] 请求终止进程 (超时" << killTimeoutMs << "ms)";
    m_killTimer->start(killTimeoutMs);
    m_process->terminate();
}

/**
 * @brief 同步终止进程并等待结束
 */
void ProcessManager::stopAndWait(int waitForFinishedMs)
{
    if (!isRunning()) {
        return;
    }

    qDebug() << "[ProcessManager] 同步终止进程 (等待" << waitForFinishedMs << "ms)";
    m_killTimer->stop();
    m_process->terminate();

    if (!m_process->waitForFinished(waitForFinishedMs)) {
        qWarning() << "[ProcessManager] terminate 超时，强制 kill 进程树";
        killProcessTree();
    }
    qDebug() << "[ProcessManager] 进程已完全退出";
}

/**
 * @brief 检查进程是否正在运行
 */
bool ProcessManager::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

/**
 * @brief 终止整个进程树（包含子进程）
 *
 * 使用 taskkill /T /F /PID 同步终止进程及其所有子进程，
 * 避免通过 cmd.exe 启动的子进程成为孤儿进程。
 */
void ProcessManager::killProcessTree()
{
    qint64 pid = m_process->processId();
    qDebug() << "[ProcessManager] killProcessTree PID:" << pid
             << "state:" << m_process->state();

    if (pid != 0) {
        // 同步执行 taskkill /T /F，等待命令完成确保整个进程树被终止
        qDebug() << "[ProcessManager] taskkill /T /F /PID" << pid;
        int ret = QProcess::execute("taskkill",
            QStringList() << "/T" << "/F" << "/PID" << QString::number(pid));
        qDebug() << "[ProcessManager] taskkill 返回:" << ret;
    }

    // 终止主进程并同步 QProcess 内部状态
    m_process->kill();
    m_process->waitForFinished(2000);

    // 兑底：若进程仍在运行，按程序名强制终止
    if (m_process->state() != QProcess::NotRunning) {
        QString prog = m_program.isEmpty() ? m_process->program() : m_program;
        if (!prog.isEmpty()) {
            QString procName = QFileInfo(prog).fileName();
            qWarning() << "[ProcessManager] 进程仍在运行，兜底 taskkill /IM" << procName;
            QProcess::execute("taskkill",
                QStringList() << "/F" << "/IM" << procName);
            m_process->waitForFinished(2000);
        }
    }
}
