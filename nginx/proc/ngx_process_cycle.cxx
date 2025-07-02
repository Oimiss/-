
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件 
#include <errno.h>    //errno
#include <unistd.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

//函数声明
static void ngx_start_worker_processes(int threadnums);
static int ngx_spawn_process(int threadnums,const char *pprocname);
static void ngx_worker_process_cycle(int inum,const char *pprocname);
static void ngx_worker_process_init(int inum);
static void addChildProcess(HANDLE hProcess, DWORD pid);

//变量声明
static u_char  master_process[] = "master process";

//描述：创建worker子进程
void ngx_master_process_cycle()
{    
    sigset_t set;        //信号集

    sigemptyset(&set);   //清空信号集

    //下列这些信号在执行本函数期间不希望收到【考虑到官方nginx中有这些信号，老师就都搬过来了】（保护不希望由信号中断的代码临界区）
    //建议fork()子进程时学习这种写法，防止信号的干扰；
    sigaddset(&set, SIGCHLD);     //子进程状态改变
    sigaddset(&set, SIGALRM);     //定时器超时
    sigaddset(&set, SIGIO);       //异步I/O
    sigaddset(&set, SIGINT);      //终端中断符
    sigaddset(&set, SIGHUP);      //连接断开
    sigaddset(&set, SIGUSR1);     //用户定义信号
    sigaddset(&set, SIGUSR2);     //用户定义信号
    sigaddset(&set, SIGWINCH);    //终端窗口大小改变
    sigaddset(&set, SIGTERM);     //终止
    sigaddset(&set, SIGQUIT);     //终端退出符
    //.........可以根据开发的实际需要往其中添加其他要屏蔽的信号......
    
    //设置，此时无法接受的信号；阻塞期间，你发过来的上述信号，多个会被合并为一个，暂存着，等你放开信号屏蔽后才能收到这些信号。。。
    //sigprocmask()在第三章第五节详细讲解过
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) //第一个参数用了SIG_BLOCK表明设置 进程 新的信号屏蔽字 为 “当前信号屏蔽字 和 第二个参数指向的信号集的并集
    {        
        ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_master_process_cycle()中sigprocmask()失败!");
    }
    //即便sigprocmask失败，程序流程 也继续往下走

    //首先我设置主进程标题---------begin
    size_t size;
    int    i;
    size = sizeof(master_process);  //注意我这里用的是sizeof，所以字符串末尾的\0是被计算进来了的
    size += g_argvneedmem;          //argv参数长度加进来    
    if(size < 1000) //长度小于这个，我才设置标题
    {
        char title[1000] = {0};
        strcpy(title,(const char *)master_process); //"master process"
        strcat(title," ");  //跟一个空格分开一些，清晰    //"master process "
        for (i = 0; i < g_os_argc; i++)         //"master process ./nginx"
        {
            strcat(title,g_os_argv[i]);
        }//end for
        ngx_setproctitle(title); //设置标题
        ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 【master进程】启动并开始运行......!",title,ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志
    }    
    //首先我设置主进程标题---------end
        
    //从配置文件中读取要创建的worker进程数量
    CConfig *p_config = CConfig::GetInstance(); //单例类
    int workprocess = p_config->GetIntDefault("WorkerProcesses",1); //从配置文件中得到要创建的worker进程数量
    ngx_start_worker_processes(workprocess);  //这里要创建worker子进程

// 声明添加子进程句柄的实现
void addChildProcess(HANDLE hProcess, DWORD pid) {
    childHandles.push_back(hProcess);
    childProcessIds.push_back(pid);
}

    //创建子进程后，父进程的执行流程会返回到这里，子进程不会走进来    
    // 存储子进程句柄和ID
std::vector<HANDLE> childHandles;
std::vector<DWORD> childProcessIds;

// 实现添加子进程句柄的函数
static void addChildProcess(HANDLE hProcess, DWORD pid) {
    childHandles.push_back(hProcess);
    childProcessIds.push_back(pid);
}

    for (;;) {
        // 等待任意子进程退出
        DWORD waitResult = WaitForMultipleObjects(
            childHandles.size(), 
            childHandles.data(), 
            FALSE, 
            INFINITE
        );

        if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + childHandles.size()) {
            int index = waitResult - WAIT_OBJECT_0;
            DWORD exitCode;
            GetExitCodeProcess(childHandles[index], &exitCode);
            ngx_log_error_core(NGX_LOG_NOTICE, 0, "子进程%d退出，退出码%d", childProcessIds[index], exitCode);

// 重启子进程
ngx_spawn_process(index, "worker process");
            CloseHandle(childHandles[index]);
            childHandles.erase(childHandles.begin() + index);

            // 重启子进程（根据需要实现）
            // ngx_spawn_process(...);
        }
    }
    return;
}

//描述：根据给定的参数创建指定数量的子进程，因为以后可能要扩展功能，增加参数，所以单独写成一个函数
//threadnums:要创建的子进程数量
static void ngx_start_worker_processes(int threadnums)
{
    int i;
    for (i = 0; i < threadnums; i++)  //master进程在走这个循环，来创建若干个子进程
    {
        ngx_spawn_process(i,"worker process");
    } //end for
    return;
}

//描述：产生一个子进程
//inum：进程编号【0开始】
//pprocname：子进程名字"worker process"
static int ngx_spawn_process(int inum,const char *pprocname)
{
    pid_t  pid;

    TCHAR cmdLine[256];
_sntprintf_s(cmdLine, sizeof(cmdLine)/sizeof(TCHAR), _T("%s worker %d"), g_os_argv[0], inum);

STARTUPINFO si = {0};
si.cb = sizeof(STARTUPINFO);
PROCESS_INFORMATION pi;

BOOL success = CreateProcess(
    NULL,                   // 模块名
    cmdLine,                // 命令行参数
    NULL,                   // 进程安全属性
    NULL,                   // 线程安全属性
    FALSE,                  // 不继承句柄
    0,                      // 创建标志
    NULL,                   // 环境变量
    NULL,                   // 当前目录
    &si,                    // 启动信息
    &pi                     // 进程信息
);

if (!success) {
    ngx_log_error_core(NGX_LOG_ALERT, GetLastError(), "ngx_spawn_process()CreateProcess()产生子进程num=%d失败!", inum);
    return -1;
}

// 将子进程句柄添加到监控列表
addChildProcess(pi.hProcess, pi.dwProcessId);
CloseHandle(pi.hThread);
return pi.dwProcessId;

    //父进程分支会走到这里，子进程流程不往下边走-------------------------
    //若有需要，以后再扩展增加其他代码......
    return pid;
}

//描述：worker子进程的功能函数，每个woker子进程，就在这里循环着了（无限循环【处理网络事件和定时器事件以对外提供web服务】）
//     子进程分叉才会走到这里
//inum：进程编号【0开始】
static void ngx_worker_process_cycle(int inum,const char *pprocname) 
{
    //设置一下变量
    ngx_process = NGX_PROCESS_WORKER;  //设置进程的类型，是worker进程

    //重新为子进程设置进程名，不要与父进程重复------
    ngx_worker_process_init(inum);
    ngx_setproctitle(pprocname); //设置标题   
    ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 【worker进程】启动并开始运行......!",pprocname,ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志


    //测试代码，测试线程池的关闭
    //sleep(5); //休息5秒        
    //g_threadpool.StopAll(); //测试Create()后立即释放的效果

    //暂时先放个死循环，我们在这个循环里一直不出来
    //setvbuf(stdout,NULL,_IONBF,0); //这个函数. 直接将printf缓冲区禁止， printf就直接输出了。
    for(;;)
    {

      

        ngx_process_events_and_timers(); //处理网络事件和定时器事件

     

    } //end for(;;)

    //如果从这个循环跳出来
    g_threadpool.StopAll();      //考虑在这里停止线程池；
    g_socket.Shutdown_subproc(); //socket需要释放的东西考虑释放；
    return;
}

//描述：子进程创建时调用本函数进行一些初始化工作
static void ngx_worker_process_init(int inum)
{
    // Windows不支持sigprocmask，移除信号屏蔽代码

    //线程池代码，率先创建，至少要比和socket相关的内容优先
    CConfig *p_config = CConfig::GetInstance();
    int tmpthreadnums = p_config->GetIntDefault("ProcMsgRecvWorkThreadCount",5); //处理接收到的消息的线程池中线程数量
    if(g_threadpool.Create(tmpthreadnums) == false)  //创建线程池中线程
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    sleep(1); //再休息1秒；

    if(g_socket.Initialize_subproc() == false) //初始化子进程需要具备的一些多线程能力相关的信息
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    
    //如下这些代码参照官方nginx里的ngx_event_process_init()函数中的代码
    g_socket.ngx_iocp_init();           //初始化IOCP相关内容，同时 往监听socket上增加监听事件，从而开始让监听端口履行其职责
    //g_socket.ngx_epoll_listenportstart();//往监听socket上增加监听事件，从而开始让监听端口履行其职责【如果不加这行，虽然端口能连上，但不会触发ngx_epoll_process_events()里边的epoll_wait()往下走】
    
    
    //....将来再扩充代码
    //....
    return;
}
