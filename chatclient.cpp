#include "chatclient.h"
#include "ui_chatclient.h"
#include "clientlogthread.h"

#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QBoxLayout>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QDataStream>
#include <QTcpSocket>
#include <QApplication>
#include <QThread>
#include <QMessageBox>
#include <QSettings>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QProgressDialog>
#include <QPixmap>

#define BLOCK_SIZE      1024

ChatClient::ChatClient(QWidget *parent) :
    QWidget(parent), isSent(false),ui(new Ui::ChatClient)
{
    ui->setupUi(this);      //ui 파일이 만들어짐

    QList<int> sizes;
    sizes << 300 << 400;
    ui->splitter->setSizes(sizes);

    QPixmap pix("C:/ChatClient/images/a.png");                                          //ui에 보여지는 이미지 지정
    ui->label_pic->setPixmap(pix);
    int w = ui->label_pic->width();                                         //이미지의 폭
    int h = ui->label_pic->height();                                        //이미지의 높이
    ui->label_pic->setPixmap(pix.scaled(w, h, Qt::KeepAspectRatio));        //이미지 크기 지정

    ui->ipAddressLineEdit->setText("127.0.0.1");                            //ip입력칸에 고정
    QRegularExpression re("^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\."      //ip자릿수 만큼만 입력 가능
                          "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\."
                          "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\."
                          "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    QRegularExpressionValidator validator(re);

    ui->ipAddressLineEdit->setPlaceholderText("Server IP Address");         //ip입력하지 않았을때 보여지는 내용
    ui->ipAddressLineEdit->setValidator(&validator);                        //ip 칸 수 제한

    ui->portNumLineEdit->setText(QString::number(PORT_NUMBER));             //포트번호 8000 고정
    ui->portNumLineEdit->setInputMask("00000;_");                           //포트번호 입력 5자리 제한
    ui->portNumLineEdit->setPlaceholderText("Server Port No");              //아무것도 없는 lineEdit에 나타나느 내용
    ui->messageTextEdit->setReadOnly(true);                                 //message 내용이 보여지는 texteEdit은 수정할 수 없도록

    connect(ui->messageLineEdit, SIGNAL(returnPressed( )), SLOT(sendData( )));                      //messege 입력칸에 내용이 엔터를 쳤을 때 보내짐
    connect(ui->messageLineEdit, SIGNAL(returnPressed( )), ui->messageLineEdit, SLOT(clear( )));    //보내고 나면 messageLineEdit clear

    connect(ui->sendPushButton, SIGNAL(clicked( )), SLOT(sendData( )));                     //sendPushButton을 클릭 했을 때 보내짐
    connect(ui->sendPushButton, SIGNAL(clicked( )), ui->messageLineEdit, SLOT(clear( )));   //보내고 나면 messageLineEdit clear
    ui->messageLineEdit->setEnabled(false);
    ui->sendPushButton->setEnabled(false);

    connect(ui->fileTransferPushButton, SIGNAL(clicked( )), SLOT(sendFile( )));     // 파일 전송 버튼 누르면 sendFile
    ui->fileTransferPushButton->setDisabled(true);                                  // 버튼 비활성화

    connect(ui->logOutPushButton, SIGNAL(clicked( )), this, SLOT(close( )));        //logout 눌렀을 때 chatclient close

    clientSocket = new QTcpSocket(this);                                    //채팅을 위한 소켓 생성
    connect(clientSocket, &QAbstractSocket::errorOccurred,
            [=]{ qDebug( ) << clientSocket->errorString( ); });
    connect(clientSocket, SIGNAL(readyRead( )), SLOT(receiveData( )));     // 읽을 준비가 되면 데이터를 읽음
    connect(clientSocket, SIGNAL(disconnected( )), SLOT(disconnect( )));

    fileClient = new QTcpSocket(this);                                              // 파일 전송을 위한 소켓
    connect(fileClient, SIGNAL(bytesWritten(qint64)), SLOT(goOnSend(qint64)));      // qint64 만큼씩 데이터를 끊어보냄
    connect(fileClient, SIGNAL(disconnected( )), fileClient, SLOT(deletelater( )));

    progressDialog = new QProgressDialog(0);    // 파일 전송 프로그레스
    progressDialog->setAutoClose(true);         // 파일이 자동으로 닫힘
    progressDialog->reset();                    // reset


    connect(ui->chatInPushButton, &QPushButton::clicked,
            [=]{
        if(ui->chatInPushButton->text() == tr("Chat In"))  {                        // chat in 상태의 버틑을 누르면
            sendProtocol(Chat_In, ui->nameLineEdit->text().toStdString().data());   // chat in과 name data가 서버로 보내짐
            ui->chatInPushButton->setText(tr("Chat Out"));                          // 버튼을 chat out으로 바꿈
            ui->messageLineEdit->setEnabled(true);                                  // 메시지 입력칸 활성화
            ui->sendPushButton->setEnabled(true);                                   // send 버튼 활성화
            ui->fileTransferPushButton->setEnabled(true);                           // 파일 전송 버튼 활성화
            ui->nameLineEdit->setDisabled(true);                                    // 로그인 nmae 입력 버튼 비활성화
            ui->ipAddressLineEdit->setDisabled(true);                               // ip, 포트번호 입력칸 비활성화
            ui->portNumLineEdit->setDisabled(true);

            QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeWidget);
            QString ip = ui->ipAddressLineEdit->text();
            QString port = ui->portNumLineEdit->text();
            QString clientName = ui->nameLineEdit->text();
            item->setText(0, ip);                                                       //ip
            item->setText(1, port);                                    //port
            item->setText(2, clientName);                                     //고객이름
            item->setText(3, "chat in");                                            //보낸 메세지 내용
            item->setText(4, QDateTime::currentDateTime().toString());                  //보낸 시간

            ui->treeWidget->addTopLevelItem(item);                               //로그를 위부터 순서대로 보여줌

            clientLogThread->appendData(item);

        }else if(ui->chatInPushButton->text() == tr("Chat Out"))  {                 // chat out 상태의 버튼을 누르면
            sendProtocol(Chat_Out, ui->nameLineEdit->text().toStdString().data());  // chat out 프로토콜과 name data를 보내줌
            ui->chatInPushButton->setText(tr("Chat In"));                           // chat in 버튼으로 바꿔줌
            ui->messageLineEdit->setDisabled(true);                                 // 메세지 입력칸 비활성화
            ui->sendPushButton->setDisabled(true);                                  // send 버튼 비활성화
            ui->fileTransferPushButton->setDisabled(true);                          // 파일 전송 버튼 비활성화
            ui->nameLineEdit->setEnabled(true);                                     // 로그인 name 입력칸 활성화
            ui->ipAddressLineEdit->setEnabled(true);                                // ip, 포트번호 입력칸 활성화
            ui->portNumLineEdit->setEnabled(true);

            QTreeWidgetItem* itemm = new QTreeWidgetItem(ui->treeWidget);         //서버에 찍히는 로그 treewidget으로 아이템 관리
            QString ipp = ui->ipAddressLineEdit->text();
            QString portt = ui->portNumLineEdit->text();
            QString clientNamee = ui->nameLineEdit->text();
            itemm->setText(0, ipp);                                                       //ip
            itemm->setText(1, portt);                                    //port
            itemm->setText(2, clientNamee);                                     //고객이름
            itemm->setText(3, "chat out");                                            //보낸 메세지 내용
            itemm->setText(4, QDateTime::currentDateTime().toString());                  //보낸 시간

            ui->treeWidget->addTopLevelItem(itemm);                               //로그를 위부터 순서대로 보여줌

            clientLogThread->appendData(itemm);

        }
    } );

    setWindowTitle(tr("Chat Client"));
    ui->chatInPushButton->setDisabled(true);        //로그인 하기 전 채팅방 입장을 막기 위해 버튼을 막음

    clientLogThread = new ClientLogThread;

    clientLogThread->start();

    resize (800, 500);
}

ChatClient::~ChatClient()           //소멸자
{
    clientSocket->close( );         //끝났으면 연결을 끊어줌
    delete ui;                      //ui delete
}

void ChatClient::closeEvent(QCloseEvent*)
{
    sendProtocol(Chat_LogOut, ui->nameLineEdit->text().toStdString().data());   //로그아웃시 입력한 이름을 서버로 보내줌
    clientSocket->disconnectFromHost();                                         //소켓과 연결 끊음
    if(clientSocket->state() != QAbstractSocket::UnconnectedState)              //소켓이 끊어지지 않은 상태라면
        clientSocket->waitForDisconnected();                                    //대기 후 끊어짐
}


void ChatClient::receiveData( )                                       // 서버에서 데이터가 올 때
{
    QTcpSocket *clientSocket = dynamic_cast<QTcpSocket *>(sender( ));   //채팅을 위한 소켓을 받은 후
    if (clientSocket->bytesAvailable( ) > BLOCK_SIZE) return;           //읽을 데이터가 있으면
    QByteArray bytearray = clientSocket->read(BLOCK_SIZE);              //1020만큼 읽어서 타입과 데이터를 확인

    Chat_Status type;       // 채팅의 목적
    char data[1020];        // 전송되는 메시지/데이터
    memset(data, 0, 1020);  // 크기가 아닌 쓰레기값을 0자체로 초기화

    QDataStream in(&bytearray, QIODevice::ReadOnly);
    in.device()->seek(0);
    in >> type;                                          // 패킷의 타입
    in.readRawData(data, 1020);                          // 실제 데이터

    QString ip = ui->ipAddressLineEdit->text();
    QString port = ui->portNumLineEdit->text();
    QString clientName = ui->nameLineEdit->text();
    QString message = ui->messageLineEdit->text();
    QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeWidget);         //서버에 찍히는 로그 treewidget으로 아이템 관리
    switch(type) {
    case Chat_Talk:                                     // 온 패킷의 타입이 대화 시작 이면
        qDebug() << "here";
        if(flag==0){                                    // 강퇴 멤버에서 메세지가 보여지는 것을 방지하기 위해 flag 사용 (초기값 0)
        ui->messageTextEdit->append(QString(data));     // 온메시지를 화면에 표시
        ui->messageLineEdit->setEnabled(true);          // 대화  message입력칸 활성화
        ui->sendPushButton->setEnabled(true);           // send 버튼 활성화
        ui->fileTransferPushButton->setEnabled(true);   // 파일 전송 버튼 활성화

//        char data[1020];        // 전송되는 메시지/데이터
//        memset(data, 0, 1020);  // 크기가 아닌 쓰레기값을 0자체로 초기화

//        QString ip = ui->ipAddressLineEdit->text();
//        QString port = ui->portNumLineEdit->text();
//        QString clientId = "0000";
//        QString clientName = ui->nameLineEdit->text();
//       // QString message = ui->messageLineEdit->text();
//        QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeWidget);


//        item->setText(0, ip);                                                       //ip
//        item->setText(1, port);                                    //port
//        item->setText(2, "client");                                     //고객이름
//        item->setText(3, QString(data));                                            //보낸 메세지 내용
//        item->setText(4, QDateTime::currentDateTime().toString());                  //보낸 시간                                       //메세지가 길어질 경우 tooltip으로 메세지 내용 보여줌

//        ui->treeWidget->addTopLevelItem(item);                               //로그를 위부터 순서대로 보여줌

//        clientLogThread->appendData(item);

        }
        else{                                               // 대화가 아니라면
        ui->messageTextEdit->setReadOnly(true);             // message 보여지는 edit은 readOnly
        ui->messageLineEdit->setDisabled(true);             // 메세지 입력칸 비활성화
        ui->sendPushButton->setDisabled(true);              // send 버튼 비활성화
        ui->fileTransferPushButton->setDisabled(true);      // 파일 전송 버튼 비활성화
        }
        break;
    case Chat_KickOut:                                          // kitout 패킷
        flag = 1;                                               // 강퇴시 flag가 1이 되면서 버튼 비활성화
        QMessageBox::critical(this, tr("Chatting Client"),      // 실행시 강퇴되었다는 메세지 보여짐
                              tr("Kick out from Server"));
        ui->messageLineEdit->setDisabled(true);                 // 버튼의 상태 변경 메세지 입력간 비활성화
        ui->sendPushButton->setDisabled(true);                  // send 버튼 비활성화
        ui->fileTransferPushButton->setDisabled(true);          // 파일 전송 버튼 비활성화
        ui->nameLineEdit->setReadOnly(false);                   // 로그인 이름 입력 칸 활성화
        ui->chatInPushButton->setDisabled(true);                // 채팅방 입장 버튼 비활성화

        item->setText(0, ip);                                                       //ip
        item->setText(1, port);                                    //port
        item->setText(2, clientName);                                     //고객이름
        item->setText(3, "kick out");
        //item->setText(4, QString(data));                                            //보낸 메세지 내용
        item->setText(4, QDateTime::currentDateTime().toString());                  //보낸 시간

        ui->treeWidget->addTopLevelItem(item);                               //로그를 위부터 순서대로 보여줌

        clientLogThread->appendData(item);
        break;
    case Chat_Invite:                                                // 초대 패킷
        flag = 0;                                                    // 초대시 flag가 0이 되면서 버튼 활성화
        QMessageBox::critical(this, tr("Chatting Client"),           // 초대 되었다는 메세지 박스
                              tr("Invited from Server"));
        ui->messageLineEdit->setEnabled(true);                       // 메세지 입력칸 활성화
        ui->sendPushButton->setEnabled(true);                        // send 버튼 활성화
        ui->fileTransferPushButton->setEnabled(true);                // 파일 전송 버튼 활성화
        ui->nameLineEdit->setReadOnly(true);                         // 로그인시 이름 입력 칸 readOnly

        item->setText(0, ip);                                                       //ip
        item->setText(1, port);                                    //port
        item->setText(2, clientName);                                     //고객이름
        item->setText(3, "invite");                                       //보낸 메세지 내용
        item->setText(4, QDateTime::currentDateTime().toString());                  //보낸 시간

        ui->treeWidget->addTopLevelItem(item);                               //로그를 위부터 순서대로 보여줌

        clientLogThread->appendData(item);

        break;

    case Chat_LogInCheck:                                                   // 로그인시 등록 고객인지 체크
        ui->nameLineEdit->clear();                                          // 등록 고객이 아니라면 name 입력칸 clear
        ui->chatInPushButton->setDisabled(true);                            // chat in 버튼 비활성화
        ui->sendPushButton->setDisabled(true);                              // send 버튼 비활성화
        ui->fileTransferPushButton->setDisabled(true);                      // file 전송 비활성화
        ui->logInPushButton->setEnabled(true);                              // 로그인 버튼 활성화
        QMessageBox::critical(this, tr("Chatting Server"),                  // 없는 고객임을 알림
                              tr(" missing customer. Please re-enter.") );
        item->setText(0, ip);                                                       //ip
        item->setText(1, port);                                    //port
        item->setText(2, clientName);                                     //고객이름
        item->setText(3, "Login fail");
        //item->setText(4, QString(data));                                            //보낸 메세지 내용
        item->setText(4, QDateTime::currentDateTime().toString());                  //보낸 시간

        ui->treeWidget->addTopLevelItem(item);                               //로그를 위부터 순서대로 보여줌

        clientLogThread->appendData(item);

        break;

    };


}
void ChatClient::disconnect( )                              /* 연결이 끊어졌을 때 : 상태 변경 */
{
    QMessageBox::critical(this, tr("Chatting Client"),      // 연결이 끊김을 알림
                          tr("Disconnect from Server"));
    ui->messageLineEdit->setEnabled(false);                 // 메세지 입력칸 비활성화
    ui->nameLineEdit->setReadOnly(false);                   // 로그인 이름 입력 칸 활성화
    ui->sendPushButton->setEnabled(false);                  // send 버튼 비활성화
    ui->logInPushButton->setText(tr("Log in"));             // login 버튼 Log in
}

void ChatClient::sendProtocol(Chat_Status type, char* data, int size) /* 프로토콜을 생성해서 서버로 전송 */
{
    QByteArray dataArray;                                             // 소켓으로 보낼 데이터를 채우고
    QDataStream out(&dataArray, QIODevice::WriteOnly);
    out.device()->seek(0);
    out << type;                                                      // 타입과
    out.writeRawData(data, size);                                     // 데이터를
    clientSocket->write(dataArray);                                   // 서버로 전송
    clientSocket->flush();
    while(clientSocket->waitForBytesWritten());


}

void ChatClient::sendData(  )                                                   /* 메시지 보내기 */
{
    QString str = ui->messageLineEdit->text( );                                 //채팅 창에 데이터 보낼 때 마다 불려진다
    if(str.length( )) {                                                         //데이터를 보낼 때
        QByteArray bytearray;
        bytearray = str.toUtf8( );
        ui->messageTextEdit->append("<font color=blue>나</font> : " + str);     /* 화면에 표시 : 앞에 '나'라고 추가 */
        sendProtocol(Chat_Talk, bytearray.data());                              // 말 할 때 서버로 메세지 데이터를 보냄

        char data[1020];        // 전송되는 메시지/데이터
        memset(data, 0, 1020);  // 크기가 아닌 쓰레기값을 0자체로 초기화

/*        QString ip = ui->ipAddressLineEdit->text();
        QString port = ui->portNumLineEdit->text();
        QString clientName = ui->nameLineEdit->text();
        QString message = ui->messageLineEdit->text();
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeWidget);


        item->setText(0, ip);                                                       //ip
        item->setText(1, port);                                    //port
        item->setText(2, clientName);                                     //고객이름
        item->setText(3, message);                                            //보낸 메세지 내용
        item->setText(4, QDateTime::currentDateTime().toString());    */              //보낸 시간                                       //메세지가 길어질 경우 tooltip으로 메세지 내용 보여줌

//        ui->treeWidget->addTopLevelItem(item);                               //로그를 위부터 순서대로 보여줌

//        clientLogThread->appendData(item);
    }
}


void ChatClient::goOnSend(qint64 numBytes)                  /* 파일 전송시 여러번 나눠서 전송 */
{                                                           // 파일을 전체를 보낼 수 없기 때문에 나눠서 전송
    byteToWrite -= numBytes;                                // 보낸 만큼 byteToWrite가 감소
    outBlock = file->read(qMin(byteToWrite, numBytes));     // numbyte 만큼 읽음
    fileClient->write(outBlock);                            // outBlock 만큼 서버로 보냄

    progressDialog->setMaximum(totalSize);
    progressDialog->setValue(totalSize-byteToWrite);

    if (byteToWrite == 0) {                                 // Send completed
        qDebug("File sending completed!");
        progressDialog->reset();
    }
}

void ChatClient::sendFile()                                 /* 파일 보내기 */
{
    loadSize = 0;
    byteToWrite = 0;
    totalSize = 0;
    outBlock.clear();

    QString filename = QFileDialog::getOpenFileName(this);
    if(filename.length()) {                                  // 파일이 있으면
        file = new QFile(filename);                          // 파일을 연다
        file->open(QFile::ReadOnly);

    qDebug() << QString("file %1 is opened").arg(filename);
    progressDialog->setValue(0);                                              //프로그레스 다이얼로그 초기값 지정
    if (!isSent) {
        fileClient->connectToHost(ui->ipAddressLineEdit->text( ),
                                  ui->portNumLineEdit->text( ).toInt( ) + 1); //파일 전송 소켓은 하나 작기 때문
        isSent = true;                                                        //초기값 false
    }

    QString name = ui->nameLineEdit->text();

    byteToWrite = totalSize = file->size();
    loadSize = 1024;

    QDataStream out(&outBlock, QIODevice::WriteOnly);
    out << qint64(0) << qint64(0) << filename << name;

    totalSize += outBlock.size();
    byteToWrite += outBlock.size();

    out.device()->seek(0);                                  //seek으로 앞으로 이동 한다
    out << totalSize << qint64(outBlock.size());            //전체 크기, 사이즈를 넣고

    fileClient->write(outBlock);                            //서버로 보낸다

    progressDialog->setMaximum(totalSize);                  //프로그레스바 최대 사이즈 설정
    progressDialog->setValue(totalSize-byteToWrite);        //최대사이즈에서 값설정을 한 후
    progressDialog->show();                                 //프로그레스바를 보여줌
    }
    qDebug() << QString("Sending file %1").arg(filename);
}


void ChatClient::on_logInPushButton_clicked()                                   //로그인 버튼을 눌렀을 때 서버로 login과 data를 보냄
{
    clientSocket->connectToHost(ui->ipAddressLineEdit->text( ),                 //입력된 ip와 포트번호로 연결
                                ui->portNumLineEdit->text( ).toInt( ));
    clientSocket->waitForConnected();
    sendProtocol(Chat_Login, ui->nameLineEdit->text().toStdString().data());
    ui->chatInPushButton->setEnabled(true);                                     //chat in 버튼 활성화
    ui->fileTransferPushButton->setDisabled(true);                              //파일 전송 버튼 비활성화

    QString ip = ui->ipAddressLineEdit->text();
    QString port = ui->portNumLineEdit->text();
    QString clientName = ui->nameLineEdit->text();

    QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeWidget);         //서버에 찍히는 로그 treewidget으로 아이템 관리
    item->setText(0, ip);                                                       //ip
    item->setText(1, port);                                    //port
    item->setText(2, clientName);                                     //고객이름
    item->setText(3, "Log in");                                            //보낸 메세지 내용
    item->setText(4, QDateTime::currentDateTime().toString());                  //보낸 시간

    ui->treeWidget->addTopLevelItem(item);                               //로그를 위부터 순서대로 보여줌

    clientLogThread->appendData(item);

}

void ChatClient::on_logOutPushButton_clicked()                                  //로그아웃 버튼 눌렀을 때 서버로 logout과 data를 보냄
{
    sendProtocol(Chat_LogOut, ui->nameLineEdit->text().toStdString().data());
    ui->nameLineEdit->setReadOnly(false);

    QString ip = ui->ipAddressLineEdit->text();
    QString port = ui->portNumLineEdit->text();
    QString clientName = ui->nameLineEdit->text();

    QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeWidget);         //서버에 찍히는 로그 treewidget으로 아이템 관리
    item->setText(0, ip);                                                       //ip
    item->setText(1, port);                                    //port
    item->setText(2, clientName);                                     //고객이름
    item->setText(3, "Log out");                                            //보낸 메세지 내용
    item->setText(4, QDateTime::currentDateTime().toString());                  //보낸 시간

    ui->treeWidget->addTopLevelItem(item);                               //로그를 위부터 순서대로 보여줌

    clientLogThread->appendData(item);
}
