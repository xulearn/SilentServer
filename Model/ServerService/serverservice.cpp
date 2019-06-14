#include "serverservice.h"

//Custom
#include "View/MainWindow/mainwindow.h"

//C++
#include <thread>
#include <ctime>


ServerService::ServerService(MainWindow* pMainWindow)
{
    serverVersion = "1.15";

    this->pMainWindow = pMainWindow;

    bWinSockStarted = false;
    bListening      = false;

    iUsersConnectedCount = 0;
}





std::string ServerService::getServerVersion()
{
    return serverVersion;
}

bool ServerService::startWinSock()
{
    pMainWindow->clearChatWindow();
    pMainWindow->printOutput( std::string("Starting...") );

    // Start Winsock2

    WSADATA WSAData;
    // Start WinSock2 (ver. 2.2)
    if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
    {
        pMainWindow->printOutput(std::string("WSAStartup function failed and returned: " + std::to_string(WSAGetLastError()) + ".\nTry again.\n"));
    }
    else
    {
        bWinSockStarted = true;

        startToListenForConnection();
        if (bListening)
        {
            pMainWindow->changeStartStopActionText(true);

            std::thread listenThread(&ServerService::listenForNewConnections, this);
            listenThread.detach();

            return true;
        }
    }

    return false;
}

void ServerService::startToListenForConnection()
{
    // Create the IPv4 TCP socket
    listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET)
    {
        pMainWindow->printOutput(std::string("ServerService::listenForConnection()::socket() function failed and returned: " + std::to_string(WSAGetLastError()) + "."));
    }
    else
    {
        pMainWindow->printOutput(std::string("Created listen socket..."));

        // Create and fill the "sockaddr_in" structure containing the IPv4 socket
        sockaddr_in myAddr;
        memset(myAddr.sin_zero, 0, sizeof(myAddr.sin_zero));
        myAddr.sin_family = AF_INET;
        // if port == 0 then that means that it will find free port
        myAddr.sin_port = htons(51337);
        myAddr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(listenSocket, reinterpret_cast<sockaddr*>(&myAddr), sizeof(myAddr)) == SOCKET_ERROR)
        {
            pMainWindow->printOutput(std::string("ServerService::listenForConnection()::bind() function failed and returned: " + std::to_string(WSAGetLastError()) + ".\nSocket will be closed. Try again.\n"));
            closesocket(listenSocket);
        }
        else
        {
            // Find out local port and show it
            sockaddr_in myBindedAddr;
            int len = sizeof(myBindedAddr);
            getsockname(listenSocket, reinterpret_cast<sockaddr*>(&myBindedAddr), &len);

            // Get my IP
            char myIP[16];
            inet_ntop(AF_INET, &myBindedAddr.sin_addr, myIP, sizeof(myIP));

            pMainWindow->printOutput(std::string("Success. Waiting for a connection requests on port: " + std::to_string(ntohs(myBindedAddr.sin_port)) + "."));

            if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
            {
                pMainWindow->printOutput(std::string("ServerService::listenForConnection()::listen() function failed and returned: " + std::to_string(WSAGetLastError()) + ".\nSocket will be closed. Try again.\n"));
                closesocket(listenSocket);
            }
            else
            {
                pMainWindow->printOutput(std::string("\n!!!!!!!!WARNING!!!!!!!!\nThe data transmitted over the network is not encrypted.\n"));

                // Translate listen socket to non-blocking mode
                u_long arg = true;
                if (ioctlsocket(listenSocket,FIONBIO,&arg) == SOCKET_ERROR)
                {
                    pMainWindow->printOutput(std::string("ServerService::listenForConnection()::ioctsocket() failed and returned: " + std::to_string(WSAGetLastError()) + ".\nSocket will be closed. Try again.\n"));
                    closesocket(listenSocket);
                }
                else
                {
                    bListening = true;
                }
            }
        }
    }
}

void ServerService::listenForNewConnections()
{
    sockaddr_in connectedWith;
    memset(connectedWith.sin_zero, 0, sizeof(connectedWith.sin_zero));
    int iLen = sizeof(connectedWith);

    // Accept new connection
    while (bListening)
    {
        // We will check if there is a message every 500 ms (look end of this function)
        SOCKET newConnectedSocket;
        newConnectedSocket = accept(listenSocket, reinterpret_cast<sockaddr*>(&connectedWith), &iLen);
        if (newConnectedSocket != INVALID_SOCKET)
        {
            pMainWindow->printOutput(std::string("\nSomeone is connecting..."), true);
            // Disable Nagle algorithm for Connected Socket
            BOOL bOptVal = true;
            int bOptLen = sizeof(BOOL);
            if (setsockopt(newConnectedSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&bOptVal), bOptLen) == SOCKET_ERROR)
            {
                pMainWindow->printOutput(std::string("ServerService::listenForNewConnections()::setsockopt() (Nagle algorithm) failed and returned: " + std::to_string(WSAGetLastError()) + ".\nSending FIN to this new user.\n"),true);

                std::thread closethread(&ServerService::sendFINtoSocket, this, newConnectedSocket);
                closethread.detach();
            }
            else
            {
                // Receive version and user name
                char nameBuffer[41];
                memset(nameBuffer, 0, 41);
                if (recv(newConnectedSocket, nameBuffer, 41, 0) > 1)
                {
                    // Received version & user name

                    // Check if client version is the same with the server version
                    char clientVersionSize = nameBuffer[0];
                    char* pVersion = new char[ static_cast<unsigned long long>(clientVersionSize + 1) ];
                    memset( pVersion, 0, static_cast<unsigned long long>(clientVersionSize + 1) );

                    std::memcpy(pVersion, nameBuffer + 1, static_cast<unsigned long long>(clientVersionSize));

                    std::string clientVersion(pVersion);
                    delete[] pVersion;
                    if ( clientVersion != serverVersion )
                    {
                        pMainWindow->printOutput(std::string("Client version " + clientVersion + " does not match with the server version " + serverVersion + "."), true);
                        char answerBuffer[21];
                        memset(answerBuffer, 0, 21);

                        answerBuffer[0] = 3;
                        answerBuffer[1] = static_cast<char>(serverVersion.size());
                        std::memcpy(answerBuffer + 2, serverVersion.c_str(), serverVersion.size());

                        send(newConnectedSocket, answerBuffer, 2 + serverVersion.size(), 0);
                        std::thread closethread(&ServerService::sendFINtoSocket, this, newConnectedSocket);
                        closethread.detach();

                        continue;
                    }

                    // Check if this user name is free
                    std::string userNameStr(nameBuffer + 1 + clientVersionSize);
                    bool bUserNameFree = true;
                    for (unsigned int i = 0; i<users.size(); i++)
                    {
                        if (users[i]->userName == userNameStr)
                        {
                            bUserNameFree = false;
                            break;
                        }
                    }

                    if (bUserNameFree == false)
                    {
                        pMainWindow->printOutput(std::string("User name " + userNameStr + " is already taken."),true);
                        char command = 0;
                        send(newConnectedSocket,reinterpret_cast<char*>(&command), 1, 0);
                        std::thread closethread(&ServerService::sendFINtoSocket, this, newConnectedSocket);
                        closethread.detach();
                    }
                    else
                    {
                        // Show with whom connected
                        char connectedWithIP[16];
                        memset(&connectedWithIP,0,16);
                        inet_ntop(AF_INET, &connectedWith.sin_addr, connectedWithIP, sizeof(connectedWithIP));

                        char tempData[1400];
                        memset(tempData, 0, 1400);

                        // we ++ new user (if something will go wrong later we will -- this user
                        iUsersConnectedCount++;


                        // Prepare online info to user.
                        // Prepared data format (amount of bytes in '()'):

                        // (1) Is user name free (if not then all other stuff is not included)
                        // (2) Packet size minus "free name" byte
                        // (4) Amount of users in main lobby (online)
                        // [
                        //      (1) Size in bytes of user name online
                        //      (usernamesize) user name
                        // ]

                        int iBytesWillSend = 0;
                        char command = 1;
                        std::memcpy(tempData, &command, 1);
                        iBytesWillSend++;

                        // We will put here packet size
                        iBytesWillSend += 2;

                        std::memcpy(tempData + iBytesWillSend, &iUsersConnectedCount, 4);
                        iBytesWillSend += 4;
                        for (unsigned int j = 0; j < users.size(); j++)
                        {
                            unsigned char nameSize = static_cast<unsigned char>(users[j]->userName.size()) + 1;
                            std::memcpy(tempData + iBytesWillSend, &nameSize, 1);
                            iBytesWillSend++;

                            std::memcpy(tempData + iBytesWillSend, users[j]->userName.c_str(), nameSize);
                            iBytesWillSend += nameSize;
                        }

                        // Put packet size to buffer (packet size - command size (1 byte) - packet size (2 bytes))
                        unsigned short int iPacketSize = static_cast<unsigned short>(iBytesWillSend - 3);
                        std::memcpy(tempData + 1, &iPacketSize, 2);

                        if (iBytesWillSend > 1350)
                        {
                            // This should happen when you got like >50 users online (when all users have name long 20 chars) if my calculations are correct.
                            // Not the main problem right now, tell me if you are suffering from this lol.

                            pMainWindow->printOutput(std::string("Server is full.\n"), true);

                            char serverIsFullCommand = 2;
                            send(newConnectedSocket,reinterpret_cast<char*>(&serverIsFullCommand), 1, 0);
                            std::thread closethread(&ServerService::sendFINtoSocket, this, newConnectedSocket);
                            closethread.detach();

                            continue;
                        }

                        // SEND
                        int iBytesWereSent = send(newConnectedSocket, tempData, iBytesWillSend, 0);

                        if (iBytesWereSent != iBytesWillSend)
                        {
                            pMainWindow->printOutput(std::string("\nWARNING:\n" + std::to_string(iBytesWereSent)+" bytes were sent of total " + std::to_string(iBytesWillSend) + " to new user.\n"), true);
                        }
                        if (iBytesWereSent == -1)
                        {
                            pMainWindow->printOutput(std::string("ServerService::listenForNewConnections()::send()) (online info) failed and returned: " + std::to_string(WSAGetLastError())+"."), true);
                            if (recv(newConnectedSocket, tempData, 1500, 0) == 0)
                            {
                                pMainWindow->printOutput(std::string("received FIN from this new user who didn't receive online info."), true);
                                shutdown(newConnectedSocket, SD_SEND);
                                if (closesocket(newConnectedSocket) != SOCKET_ERROR)
                                {
                                    pMainWindow->printOutput(std::string("closed this socket with success."), true);
                                }
                                else
                                {
                                    pMainWindow->printOutput(std::string("can't close this socket... meh. You better reboot the server..."), true);
                                }
                            }
                            iUsersConnectedCount--;
                        }
                        else
                        {
                            // Translate new connected socket to non-blocking mode
                            u_long arg = true;
                            if (ioctlsocket(newConnectedSocket, FIONBIO, &arg) == SOCKET_ERROR)
                            {
                                pMainWindow->printOutput(std::string("ServerService::listenForNewConnections()::ioctsocket() (non-blocking mode) failed and returned: " + std::to_string(WSAGetLastError()) + "."), true);

                                std::thread closethread(&ServerService::sendFINtoSocket, this, newConnectedSocket);
                                closethread.detach();
                            }
                            else
                            {
                                if (users.size() != 0)
                                {
                                    // Tell other users about new user

                                    char newUserInfo[31];
                                    memset(newUserInfo, 0, 31);

                                    unsigned char sizeOfUserName = static_cast<unsigned char>(userNameStr.size());

                                    unsigned char iSendSize = 0;

                                    // 0 - means 'there is new user, update your OnlineCount and add him to list
                                    unsigned char commandType = 0;
                                    std::memcpy(newUserInfo, &commandType, 1);
                                    iSendSize++;

                                    // Put packet size
                                    unsigned char iPacketSize = 4 + 1 + sizeOfUserName;
                                    std::memcpy(newUserInfo + 1, &iPacketSize, 1);
                                    iSendSize++;

                                    std::memcpy(newUserInfo + 2, &iUsersConnectedCount, 4);
                                    iSendSize += 4;

                                    std::memcpy(newUserInfo + 6, &sizeOfUserName, 1);
                                    iSendSize++;
                                    std::memcpy(newUserInfo + 7, userNameStr.c_str(), sizeOfUserName);
                                    iSendSize += sizeOfUserName;

                                    // Send this data
                                    for (unsigned int i = 0; i < users.size(); i++)
                                    {
                                        send(users[i]->userSocket, newUserInfo, iSendSize, 0);
                                    }
                                }


                                // Fill UserStruct for new user

                                users.push_back(new UserStruct());
                                users.back()->userName       = userNameStr;
                                users.back()->userSocket     = newConnectedSocket;
                                users.back()->pDataFromUser  = new char[1500];
                                users.back()->userIP         = std::string(connectedWithIP);
                                users.back()->userPort       = ntohs(connectedWith.sin_port);
                                users.back()->keepAliveTimer = clock();

                                // Ready to send and receive data

                                pMainWindow->printOutput(std::string("Connected with " + std::string(connectedWithIP) + ":" + std::to_string(ntohs(connectedWith.sin_port)) + " AKA " + users[ users.size() - 1 ]->userName + "."),true);
                                pMainWindow->updateOnlineUsersCount(iUsersConnectedCount);
                                users.back()->pListItem = pMainWindow->addNewUserToList(users[ users.size() - 1 ]->userName);

                                std::thread listenThread(&ServerService::listenForMessage, this, users[ users.size() - 1]);
                                listenThread.detach();
                            }
                        }
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
}

void ServerService::listenForMessage(UserStruct* userToListen)
{
    while(bListening)
    {
        while (recv(userToListen->userSocket, userToListen->pDataFromUser, 0, 0) == 0)
        {
            // There are some data to read

            int receivedAmount = recv(userToListen->userSocket, userToListen->pDataFromUser, 1, 0);
            if (receivedAmount == 0)
            {
                // Client sent FIN

                responseToFIN(userToListen);

                // Stop thread
                return;
            }
            else
            {
                if (userToListen->pDataFromUser[0] == 10)
                {
                    // This is a message (in main lobby), send it to all in main lobby

                    userToListen->keepAliveTimer = clock();

                    getMessage(userToListen);
                }
            }
        };

        clock_t timePassed = clock() - userToListen->keepAliveTimer;
        float timePassedInSeconds = static_cast<float>(timePassed)/CLOCKS_PER_SEC;
        if (timePassedInSeconds > 30)
        {
            // User was inactive for 30 seconds
            // Check if he's alive

            char keepAliveChar = 9;
            send(userToListen->userSocket, &keepAliveChar, 1, 0);

            // Translate user socket to blocking mode
            u_long arg = false;
            ioctlsocket(userToListen->userSocket, FIONBIO, &arg);

            // Set recv() time out time to 10 seconds
            DWORD time = 10000;
            setsockopt(userToListen->userSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&time), sizeof(time));

            keepAliveChar = 0;
            int returnCode = recv(userToListen->userSocket, &keepAliveChar, 1, 0);
            if (returnCode >= 0)
            {
                userToListen->keepAliveTimer = clock();

                // Translate user socket to non-blocking mode
                arg = true;
                ioctlsocket(userToListen->userSocket, FIONBIO, &arg);

                if (keepAliveChar == 10) getMessage(userToListen);
                else if (keepAliveChar == 0) responseToFIN(userToListen);
            }
            else
            {
                // We lost connection with this user
                responseToFIN(userToListen, true);

                // Stop thread
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void ServerService::getMessage(UserStruct *userToListen)
{
    // Get message size
    unsigned short int iMessageSize = 0;
    recv(userToListen->userSocket, reinterpret_cast<char*>(&iMessageSize), 2, 0);


    // Get local time
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);

    // Create string to send in format: "Hour:Minute. UserName: Message".
    std::string timeString = "";
    if (std::to_string(timeinfo.tm_hour).size() == 1)
    {
        timeString += "0";
    }
    timeString += std::to_string(timeinfo.tm_hour);
    timeString += ":";
    if (std::to_string(timeinfo.tm_min).size() == 1)
    {
        timeString += "0";
    }
    timeString += std::to_string(timeinfo.tm_min);
    timeString += ". ";
    timeString += userToListen->userName;
    timeString += ": ";


    // Add 'timeString' size and 'iMessageSize' to 'iPacketSize'
    unsigned short int iPacketSize = static_cast<unsigned short int>(timeString.size() + iMessageSize);

    // Prepare buffer to send
    char* pSendToAllBuffer = new char[3 + iPacketSize + 1];
    memset(pSendToAllBuffer, 0, 3 + iPacketSize + 1);

    // Set packet ID (message) to buffer
    pSendToAllBuffer[0] = 10;
    // Set packet size to buffer
    std::memcpy(pSendToAllBuffer + 1, &iPacketSize, 2);
    // Copy time and name to buffer
    std::memcpy(pSendToAllBuffer + 3, timeString.c_str(), timeString.size());


    // Receive user message to send
    recv(userToListen->userSocket, pSendToAllBuffer + 3 + timeString.size(), iMessageSize, 0);

    int returnCode = 0;

    // Send message to all but not the sender
    for (unsigned int j = 0; j < users.size(); j++)
    {
        returnCode = send(users[j]->userSocket, pSendToAllBuffer, 3 + iPacketSize, 0);
        if ( returnCode != (3 + iPacketSize) )
        {
            if (returnCode == SOCKET_ERROR)
            {
                pMainWindow->printOutput( std::string( "ServerService::getMessage::send() function failed and returned: " + std::to_string(WSAGetLastError()) ), true);
            }
            else
            {
                pMainWindow->printOutput( std::string( userToListen->userName + "'s message wasn't fully sent. send() returned: " + std::to_string(returnCode) ), true);
            }
        }
    }

    delete[] pSendToAllBuffer;
}



void ServerService::sendFINtoSocket(SOCKET socketToClose)
{
    // Translate socket to blocking mode
    u_long arg = true;
    if (ioctlsocket(socketToClose, FIONBIO, &arg) == SOCKET_ERROR)
    {
        pMainWindow->printOutput(std::string("ServerService::sendFINtoSocket()::ioctlsocket() (Set blocking mode) function failed and returned: "
                                             + std::to_string(WSAGetLastError()) + ".\nJust closing this socket.\n"),true);
        closesocket(socketToClose);
    }
    else
    {
        int returnCode = shutdown(socketToClose, SD_SEND);
        if (returnCode == SOCKET_ERROR)
        {
            pMainWindow->printOutput(std::string("ServerService::sendFINtoSocket()::shutdown() function failed and returned: " + std::to_string(WSAGetLastError()) + "."),true);
            closesocket(socketToClose);
        }
        else
        {
            char tempBuffer[5];
            returnCode = recv(socketToClose, tempBuffer, 5, 0);
            if (returnCode == 0)
            {
                returnCode = closesocket(socketToClose);
                if (returnCode == SOCKET_ERROR)
                {
                    pMainWindow->printOutput(std::string("ServerService::sendFINtoSocket()::closesocket() function failed and returned: " + std::to_string(WSAGetLastError()) + ".\nShutdown done but can't close socket... meh. You better reboot the server...\n"),true);
                }
                else
                {
                    pMainWindow->printOutput(std::string("Received FIN and closed socket."),true);
                }
            }
            else
            {
                returnCode = recv(socketToClose, tempBuffer, 5, 0);
                if (returnCode == 0)
                {
                    returnCode = closesocket(socketToClose);
                    if (returnCode == SOCKET_ERROR)
                    {
                        pMainWindow->printOutput(std::string("ServerService::sendFINtoSocket()::closesocket() function failed and returned: " + std::to_string(WSAGetLastError()) + ".\nShutdown done but can't close socket... meh. You better reboot the server...\n"),true);

                    }
                    else
                    {
                        pMainWindow->printOutput(std::string("Received FIN and closed socket."),true);
                    }
                }
            }
        }
    }
}

void ServerService::responseToFIN(UserStruct* userToClose, bool bUserLost)
{
    mtxUsersWrite.lock();


    if (!bUserLost)
    {
        // Client sent FIN
        // We are responding:
        pMainWindow->printOutput(std::string("\n" + userToClose->userName + " has sent FIN."),true);

        int returnCode = shutdown(userToClose->userSocket, SD_SEND);
        if (returnCode == SOCKET_ERROR)
        {
            pMainWindow->printOutput(std::string("ServerService::responseToFIN()::shutdown() function failed and returned: " + std::to_string(WSAGetLastError()) + "."),true);
            returnCode = shutdown(userToClose->userSocket, SD_SEND);
            if (returnCode == SOCKET_ERROR)
            {
                pMainWindow->printOutput(std::string("Try #2. Can't shutdown socket. Closing socket..."),true);
                returnCode = closesocket(userToClose->userSocket);
                if (returnCode == SOCKET_ERROR)
                {
                    pMainWindow->printOutput(std::string("ServerService::responseToFIN()::closesocket() function failed and returned: " + std::to_string(WSAGetLastError()) + ".\n Can't even close this socket... meh. You better reboot server...\n"),true);
                }
            }
            else
            {
                pMainWindow->printOutput(std::string("Try #2. Shutdown success."),true);
                returnCode = closesocket(userToClose->userSocket);
                if (returnCode == SOCKET_ERROR)
                {
                    pMainWindow->printOutput(std::string("ServerService::responseToFIN()::closesocket() function failed and returned: " + std::to_string(WSAGetLastError()) + ".\n Can't even close this socket... meh. You better reboot server...\n"),true);
                }
                else
                {
                    pMainWindow->printOutput(std::string("Successfully closed a socket."),true);
                }
            }
        }
        else
        {
            returnCode = closesocket(userToClose->userSocket);
            if (returnCode == SOCKET_ERROR)
            {
                pMainWindow->printOutput(std::string("ServerService::responseToFIN()::closesocket() function failed and returned: " + std::to_string(WSAGetLastError()) + ".\n Can't even close this socket... meh. You better reboot server...\n"),true);
            }
            else
            {
                pMainWindow->printOutput(std::string("Successfully closed connection with " + userToClose->userName + "."),true);
            }
        }
    }
    else
    {
        pMainWindow->printOutput(std::string("\nLost connection with " + userToClose->userName + ". Closing socket..."), true);
        closesocket(userToClose->userSocket);
    }

    iUsersConnectedCount--;
    pMainWindow->updateOnlineUsersCount(iUsersConnectedCount);


    // Clear 'users' massive

    if (users.size() - 1 != 0)
    {
        // Tell other users that one is disconnected
        char sendBuffer[31];
        memset(sendBuffer, 0, 31);

        // 1 means that someone is disconnected
        unsigned char commandType = 1;
        std::memcpy(sendBuffer, &commandType, 1);

        // Put size
        unsigned char iPacketSize = static_cast<unsigned char>(4 + userToClose->userName.size());
        std::memcpy(sendBuffer + 1, &iPacketSize, 1);

        std::memcpy(sendBuffer + 2, &iUsersConnectedCount, 4);

        std::memcpy(sendBuffer + 6, userToClose->userName.c_str(), userToClose->userName.size());

        for (unsigned int j = 0; j < users.size(); j++)
        {
            send(users[j]->userSocket, sendBuffer, 1 + 1 + 4 + userToClose->userName.size(), 0);
        }
    }

    // Erase user from massive
    for (unsigned int i = 0; i < users.size(); i++)
    {
        if (users[i]->userName == userToClose->userName)
        {
            delete[] userToClose->pDataFromUser;
            pMainWindow->deleteUserFromList(userToClose->pListItem);
            delete users[i];
            users.erase(users.begin() + i);
            break;
        }
    }


    mtxUsersWrite.unlock();
}

void ServerService::shutdownAllUsers()
{
    if (users.size() != 0)
    {
        pMainWindow->printOutput(std::string("\nShutting down...\nStating to close all sockets.\nPlease don't close programm until all sockets will be closed.\n"
                                             "The delay may be caused by the fact that some user does not send the FIN packet."));
        // Now we will not listen for new sockets and we also
        // will not listen connected users (because in listenForNewMessage function while cycle will fail)
        bListening = false;

        std::vector<bool> closedSockets(users.size());
        int correctlyClosedSocketsCount = 0;

        int returnCode;

        // We send FIN to all
        for (unsigned int i = 0; i < users.size(); i++)
        {
            returnCode = shutdown(users[i]->userSocket,SD_SEND);
            if (returnCode == SOCKET_ERROR)
            {
                pMainWindow->printOutput(std::string("ServerService::shutdownAllUsers()::shutdown() function failed and returned: " + std::to_string(WSAGetLastError()) + ". Just closing it...."));
                closesocket(users[i]->userSocket);

                // There was an error. We emergency shut it down.
                closedSockets[i] = true;

                // Delete user's read buffer & delete him from list
                delete[] users[i]->pDataFromUser;

                pMainWindow->deleteUserFromList(users[i]->pListItem);
                delete users[i];
            }
        }


        // Translate all sockets to blocking mode
        for (unsigned int i = 0; i < users.size(); i++)
        {
            if (closedSockets[i] == false)
            {
                // Socket isn't closed
                u_long arg = false;
                if (ioctlsocket(users[i]->userSocket,FIONBIO,&arg) == SOCKET_ERROR)
                {
                    pMainWindow->printOutput(std::string("ServerService::shutdownAllUsers()::ioctsocket() (set blocking mode) failed and returned: " + std::to_string(WSAGetLastError()) + ". Just closing it..."));
                    closesocket(users[i]->userSocket);

                    // There was an error. We emergency shut it down.
                    closedSockets[i] = true;

                    // Delete user's read buffer & delete him from list
                    delete[] users[i]->pDataFromUser;

                    pMainWindow->deleteUserFromList(users[i]->pListItem);
                    delete users[i];
                }
            }
        }

        bool tryAgainToClose = false;
        pMainWindow->printOutput(std::string("Sent FIN packets to all users. Waiting for a response..."));

        // We are waiting for a response
        for (unsigned int i = 0; i < users.size(); i++)
        {
            if (closedSockets[i] == false)
            {
                // Socket isn't closed
                returnCode = recv(users[i]->userSocket,users[i]->pDataFromUser,1500,0);
                if (returnCode == 0)
                {
                    // FIN received
                    returnCode = closesocket(users[i]->userSocket);
                    if (returnCode == SOCKET_ERROR)
                    {
                        pMainWindow->printOutput(std::string("ServerService::shutdownAllUsers()::closesocket() function failed and returned: " + std::to_string(WSAGetLastError()) + "."));
                    }
                    else
                    {
                        correctlyClosedSocketsCount++;
                    }

                    // There was an error. We emergency shut it down.
                    closedSockets[i] = true;

                    // Delete user's read buffer & delete him from list
                    delete[] users[i]->pDataFromUser;

                    pMainWindow->deleteUserFromList(users[i]->pListItem);
                    delete users[i];
                }
                else
                {
                    tryAgainToClose = true;
                }
            }
        }

        if (tryAgainToClose)
        {
            // Try again to close the sockets that does not returned FIN
            for (unsigned int i = 0; i < users.size(); i++)
            {
                if (closedSockets[i] == false)
                {
                    // Socket isn't closed
                    returnCode = recv(users[i]->userSocket,users[i]->pDataFromUser,1500,0);
                    if (returnCode == 0)
                    {
                        // FIN received
                        returnCode = closesocket(users[i]->userSocket);
                        if (returnCode == SOCKET_ERROR)
                        {
                            pMainWindow->printOutput(std::string("ServerService::shutdownAllUsers()::closesocket() function failed and returned: " + std::to_string(WSAGetLastError()) + "."));
                        }
                        else
                        {
                            correctlyClosedSocketsCount++;
                        }
                    }
                    else
                    {
                        pMainWindow->printOutput(std::string("FIN wasn't received from client for the second time... Closing socket..."));
                    }

                    // There was an error. We emergency shut it down.
                    closedSockets[i] = true;

                    // Delete user's read buffer & delete him from list
                    delete[] users[i]->pDataFromUser;

                    pMainWindow->deleteUserFromList(users[i]->pListItem);
                    delete users[i];
                }
            }
        }

        pMainWindow->printOutput(std::string("Correctly closed sockets: " + std::to_string(correctlyClosedSocketsCount) + "/" + std::to_string(users.size()) + "."));

        closesocket(listenSocket);

        // Clear users massive
        users.clear();

        iUsersConnectedCount = 0;

        if (bWinSockStarted)
        {
            if (WSACleanup() == SOCKET_ERROR)
            {
                pMainWindow->printOutput(std::string("ServerService::shutdownAllUsers()::WSACleanup() function failed and returned: " + std::to_string(WSAGetLastError()) + "."));
            }
            else
            {
                bWinSockStarted = false;
            }
        }
    }
    else
    {
        bListening = false;
        closesocket(listenSocket);
    }

    pMainWindow->printOutput( std::string("\nServer stopped."));
    pMainWindow->changeStartStopActionText(false);
}
