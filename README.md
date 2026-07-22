# 基于 Qt 的 TCP 文件传输系统

## 项目简介

本项目是一个基于 Qt/C++ 开发的 C/S 架构文件传输系统，包含客户端 `MyClient` 和服务端 `MyServer` 两个 Qt Widgets 应用。系统支持客户端与服务端建立 TCP 连接，实现文本消息通信、文件分块传输、传输进度显示、异常断开后的断点续传等功能。

项目针对 TCP 字节流通信中常见的粘包、半包问题设计了自定义应用层协议，并将文件传输逻辑拆分到独立线程中执行，避免大文件传输影响主界面响应。

## 技术栈

- C++11
- Qt 5.12.9
- Qt Widgets
- QTcpServer / QTcpSocket
- QThread + Worker
- QFile / QFileInfo
- QDataStream
- TCP 自定义应用层协议

## 核心功能

- 客户端与服务端 TCP 连接管理
- 文本消息收发
- 自定义协议封包/拆包
- 文件分块传输
- 客户端发送进度显示
- 服务端接收进度显示
- 异常断开后断点续传
- 同名文件更新时间校验
- 文件传输子线程处理
- 文本通信与文件传输使用独立 socket

## 项目结构

```text
csFileTransSys
├── client
│   ├── MyClient
│   │   ├── main.cpp
│   │   ├── myclient.h / myclient.cpp
│   │   ├── filesenderworker.h / filesenderworker.cpp
│   │   ├── myclient.ui
│   │   └── MyClient.pro
│   └── build-MyClient-...
│
├── server
│   ├── MyServer
│   │   ├── main.cpp
│   │   ├── myserver.h / myserver.cpp
│   │   ├── filereceiverworker.h / filereceiverworker.cpp
│   │   ├── filetcpserver.h / filetcpserver.cpp
│   │   ├── myserver.ui
│   │   └── MyServer.pro
│   └── build-MyServer-...
│
└── common
    └── protocol
        ├── protocol.h
        ├── protocol.cpp
        └── protocol.pro
```

## 自定义 TCP 协议

TCP 是面向字节流的协议，不能保证一次 `write()` 对应一次 `readyRead()`。为了解决粘包和半包问题，项目在 `common/protocol` 中实现了统一的应用层协议。

协议包结构如下：

```text
[ Magic ][ Version ][ Type ][ BodySize ][ Body ]
  4字节      2字节      2字节      4字节       N字节
```

字段说明：

- `Magic`：协议魔数，用于判断是否为本系统协议包。
- `Version`：协议版本号。
- `Type`：消息类型。
- `BodySize`：包体长度，用于判断一个完整包是否接收完成。
- `Body`：实际业务数据。

目前支持的消息类型：

```cpp
Text       // 文本消息
FileInfo   // 文件基本信息
FileChunk  // 文件数据块
FileEnd    // 文件传输结束
FileResume // 服务端返回续传位置
```

接收端会将 socket 读取到的数据先放入缓存区，然后循环调用 `tryTakePacket()` 解析完整数据包。如果缓存中只有半包，则等待下一次数据到达；如果缓存中有多个包，则连续拆包处理。

## 文件传输流程

文件传输使用独立 TCP 连接，默认端口规则为：

```text
消息通信端口：界面中设置的端口，例如 8000
文件传输端口：消息通信端口 + 1，例如 8001
```

传输流程：

```text
1. 客户端选择文件
2. 客户端启动 FileSenderWorker 子线程
3. FileSenderWorker 连接服务端文件端口
4. 客户端发送 FileInfo：文件名、文件大小、最后修改时间
5. 服务端 FileReceiverWorker 检查已有文件状态
6. 服务端返回 FileResume：已接收字节数
7. 客户端 QFile::seek(offset) 跳转到续传位置
8. 客户端按固定大小读取文件块并发送 FileChunk
9. 服务端校验 offset 后写入 .part 临时文件
10. 客户端发送 FileEnd
11. 服务端确认接收完整后将 .part 改名为正式文件
```

## 断点续传设计

服务端接收文件时不会直接写入正式文件，而是先写入：

```text
received/文件名.part
```

如果传输过程中连接异常断开，`.part` 文件会被保留。下次客户端重新发送同一个文件时，服务端会读取 `.part` 文件大小，并通过 `FileResume` 包通知客户端从指定偏移量继续发送。

客户端收到续传位置后调用：

```cpp
sendFile.seek(offset);
```

从服务端已接收的位置继续读取文件，避免重复传输。

## 文件更新校验

为了避免同名文件内容已经更新但服务端错误续传旧 `.part` 文件，客户端会在 `FileInfo` 中发送源文件最后修改时间。

服务端会为每个文件保存元信息文件：

```text
received/文件名.meta
```

其中记录：

```text
文件大小
源文件最后修改时间
```

当同名文件再次传入时，如果服务端发现文件大小或最后修改时间发生变化，会删除旧的正式文件、`.part` 文件和 `.meta` 文件，并从 0 开始重新接收。

## 多线程设计

项目使用 `QThread + Worker` 模式处理文件传输。

客户端：

```text
主线程 MyClient
├── 负责 UI 显示
├── 负责文本消息 socket
└── 启动 FileSenderWorker 子线程发送文件
```

服务端：

```text
主线程 MyServer
├── 负责 UI 显示
├── 负责文本消息 socket
├── 监听文件传输端口
└── 为文件连接创建 FileReceiverWorker 子线程
```

子线程不会直接操作 UI，而是通过信号通知主线程：

```cpp
progressChanged(int value)
message(const QString &text)
finished()
```

主线程接收信号后更新进度条和日志显示，符合 Qt UI 只能在主线程更新的原则。

## 构建方式

开发环境：

```text
Qt 5.12.9
MinGW 64-bit
Qt Creator
```

构建步骤：

1. 使用 Qt Creator 打开服务端工程：

```text
server/MyServer/MyServer.pro
```

2. 使用 Qt Creator 打开客户端工程：

```text
client/MyClient/MyClient.pro
```

3. 分别执行：

```text
qmake
构建项目
```

4. 先运行服务端 `MyServer`，再运行客户端 `MyClient`。

## 使用说明

1. 启动服务端。
2. 设置监听 IP 和端口，例如：

```text
IP: 0.0.0.0
Port: 8000
```

3. 点击“启动监听”。
4. 启动客户端。
5. 本机测试时服务器 IP 建议选择：

```text
127.0.0.1
```

6. 端口填写服务端消息端口，例如：

```text
8000
```

7. 点击“连接服务器”。
8. 文本框中输入内容，点击“发送消息”即可进行文本通信。
9. 点击“选择文件”，再点击“发送文件”即可进行文件传输。

注意：文件传输端口会自动使用服务端端口 + 1，因此如果消息端口是 `8000`，文件端口就是 `8001`。请确保两个端口都没有被其他程序占用。

## 接收文件位置

服务端接收到的文件默认保存在服务端程序运行目录下：

```text
received/
```

例如 Debug 构建下通常为：

```text
server/build-MyServer-Desktop_Qt_5_12_9_MinGW_64_bit-Debug/debug/received/
```

传输过程中会出现：

```text
文件名.part  // 未完成的临时文件
文件名.meta  // 文件大小和修改时间元信息
```

传输完成后 `.part` 文件会被改名为正式文件。

## 项目亮点

- 使用自定义 TCP 协议解决粘包、半包问题。
- 支持文本消息和文件传输两类业务。
- 支持大文件分块传输，避免一次性加载文件。
- 支持断点续传，提高异常断开后的恢复能力。
- 支持同名文件更新时间校验，避免错误续传旧文件。
- 使用 `QThread + Worker` 将文件传输逻辑放入子线程。
- 文本通信和文件传输使用独立 socket，降低相互阻塞。
- 使用 Qt 信号槽实现跨线程进度更新和日志通知。

## 后续优化方向

- 增加文件传输取消、暂停、继续按钮。
- 增加多文件批量传输。
- 增加文件 MD5/SHA256 校验。
- 增加传输速度和剩余时间显示。
- 支持服务端选择接收目录。
- 支持多客户端同时文本通信。
- 对传输协议增加错误码和确认应答机制。
