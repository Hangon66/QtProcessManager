#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QStringList>

/**
 * @brief 通用后台进程管理模块
 *
 * 封装 QProcess，提供通用的终端命令启动/停止能力。
 * 支持两种执行模式：
 * - 模式一（直接执行）：适用于独立可执行文件，如 caddy.exe、ipconfig
 * - 模式二（Shell 执行）：通过 cmd.exe /c 执行，支持 copy、dir 等内置命令及管道、重定向语法
 *
 * 无 UI，纯后台逻辑，可被任意模块实例化使用。
 */
class ProcessManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     *
     * 创建 QProcess 实例并连接内部信号。
     *
     * @param parent 父对象指针，默认为 nullptr
     */
    explicit ProcessManager(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     *
     * 若进程仍在运行，先 terminate 再 kill 确保子进程已终止。
     */
    ~ProcessManager();

    // ===== 模式一：直接执行（适用于独立可执行文件） =====

    /**
     * @brief 设置要执行的命令（程序路径 + 参数列表）
     *
     * 程序路径可以是完整路径（如 "C:/tools/caddy.exe"）或 PATH 中的命令名（如 "ipconfig"）。
     * 调用后需通过 start() 启动进程。
     *
     * @param program 可执行文件路径或命令名
     * @param arguments 参数列表，默认为空
     */
    void setCommand(const QString &program, const QStringList &arguments = {});

    /**
     * @brief 设置工作目录
     *
     * 进程启动时使用的工作目录，例如 Caddyfile 所在目录。
     * 可选，不调用则使用当前目录。
     *
     * @param dir 工作目录路径
     */
    void setWorkingDirectory(const QString &dir);

    /**
     * @brief 启动进程（直接执行模式）
     *
     * 使用 setCommand() 设置的程序路径和参数启动子进程。
     * 若进程已在运行，则忽略本次调用并发出警告日志。
     */
    void start();

    // ===== 模式二：Shell 执行 =====

    /**
     * @brief 通过 cmd.exe /c 执行完整命令行
     *
     * 支持 Windows 内置命令（copy、dir、del 等）以及管道（|）、重定向（>）等 shell 语法。
     * 若进程已在运行，则忽略本次调用并发出警告日志。
     *
     * @param commandLine 完整的命令行字符串，如 "copy a.txt b.txt" 或 "dir | findstr xxx"
     */
    void executeShell(const QString &commandLine);

    // ===== 通用控制 =====

    /**
     * @brief 优雅终止进程
     *
     * 先调用 terminate() 请求进程退出，若在 killTimeoutMs 毫秒内未退出，
     * 则调用 kill() 强制终止。
     *
     * @param killTimeoutMs 等待优雅退出的超时时间（毫秒），默认 3000ms
     */
    void stop(int killTimeoutMs = 3000);

    /**
     * @brief 同步终止进程并等待结束
     *
     * 先调用 terminate()，等待 waitForFinishedMs 毫秒；
     * 若未退出则调用 killProcessTree() 强制终止整个进程树。
     * 该方法会阻塞直到进程完全退出，适用于程序退出前的清理。
     *
     * @param waitForFinishedMs 等待优雅退出的超时时间（毫秒），默认 3000ms
     */
    void stopAndWait(int waitForFinishedMs = 3000);

    /**
     * @brief 检查进程是否正在运行
     *
     * @return true 表示进程正在运行；false 表示未运行或已退出
     */
    bool isRunning() const;

signals:
    /**
     * @brief 进程已成功启动
     */
    void started();

    /**
     * @brief 进程已结束
     *
     * @param exitCode 进程退出码
     * @param exitStatus 退出状态（正常退出或崩溃）
     */
    void finished(int exitCode, QProcess::ExitStatus exitStatus);

    /**
     * @brief 收到标准输出内容
     *
     * @param text 标准输出的文本内容（已去除末尾换行）
     */
    void outputReceived(const QString &text);

    /**
     * @brief 收到标准错误内容
     *
     * @param text 标准错误的文本内容（已去除末尾换行）
     */
    void errorReceived(const QString &text);

    /**
     * @brief 进程发生错误
     *
     * @param error 错误类型枚举值
     */
    void errorOccurred(QProcess::ProcessError error);

private:
    /**
     * @brief 内部 QProcess 对象，管理子进程生命周期
     */
    QProcess *m_process;

    /**
     * @brief 待执行的程序路径（模式一使用）
     */
    QString m_program;

    /**
     * @brief 待执行的参数列表（模式一使用）
     */
    QStringList m_arguments;

    /**
     * @brief 工作目录路径
     */
    QString m_workingDirectory;

    /**
     * @brief 强制终止定时器，用于 terminate 超时后 kill
     */
    QTimer *m_killTimer;

    /**
     * @brief 终止整个进程树（包含子进程）
     *
     * Windows 上通过 taskkill /T /F /PID 终止进程及其所有子进程，
     * 避免通过 cmd.exe 启动的子进程成为孤儿进程。
     */
    void killProcessTree();
};

#endif // PROCESSMANAGER_H
