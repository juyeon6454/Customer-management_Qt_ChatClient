#ifndef CHATCLIENT_H
#define CHATCLIENT_H

#include <QWidget>
#include <QDataStream>


class QTextEdit;
class QLineEdit;
class QTcpSocket;
class QPushButton;
class QFile;
class QProgressDialog;
class ClientLogThread;

namespace Ui {
class ChatClient;
}

typedef enum {              // 프로토콜 타입 구조체
    Chat_Login,             // 로그인(서버 접속)   --> 초대를 위한 정보 저장
    Chat_In,                // 채팅방 입장
    Chat_Talk,              // 채팅
    Chat_Out,               // 채팅방 퇴장         --> 초대 가능
    Chat_LogOut,            // 로그 아웃(서버 단절) --> 초대 불가능
    Chat_Invite,            // 초대
    Chat_KickOut,           // 강퇴
    Chat_LogInCheck,        // 가입 고객인지 체크
} Chat_Status;

class ChatClient : public QWidget
{
    Q_OBJECT

public:
    const int PORT_NUMBER = 8000;                       //포트넘버 8000으로 고정

    explicit ChatClient(QWidget *parent = nullptr);     //자유로운 형변환을 막아줌
    ~ChatClient();


private slots:
    void receiveData( );                                // 서버에서 데이터가 올 때
    void sendData( );                                   // 서버로 데이터를 보낼 때
    void disconnect( );                                 // 서버 연결이 끊 길 때 상태변경
    void sendProtocol(Chat_Status, char*, int = 1020);  // 프로토콜을 생성해서 서버로 전송
    void sendFile();                                    // 파일을 보낼 때
    void goOnSend(qint64);                              // 파일 전송시 여러번 나눠서 전송


    void on_logInPushButton_clicked();                  // 로그인 버튼 눌렀을 때
    void on_logOutPushButton_clicked();                 // 로그아웃 버튼 눌렀을 대

private:
    void closeEvent(QCloseEvent*) override;   //close 할 때 logout 타입과 name 데이터를 보냄

    Ui::ChatClient *ui;
    QTcpSocket *clientSocket;                 // 클라이언트용 소켓
    QTcpSocket *fileClient;                   // 파일 전송 소켓
    QProgressDialog* progressDialog;          // 파일 진행 확인
    QFile* file;                              // 파일 생성
    qint64 loadSize;
    qint64 byteToWrite;
    qint64 totalSize;                         /*파일 사이즈*/
    QByteArray outBlock;                      //데이터
    bool isSent;
    ClientLogThread* clientLogThread;

    int flag = 0;                             //강퇴된 멤버에게 메세지가 보이지 않도록
};

#endif // CHATCLIENT_H
