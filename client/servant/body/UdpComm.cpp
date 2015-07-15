#include "StdAfx.h"
#include <WS2tcpip.h>
#include "UdpComm.h"
#include "UdpDefines.h"
#include <string>
#include "resource.h"
#include "file/MyFile.h"
#include "VtcpBinary.h"

UdpComm::UdpComm(BOOL isSecure):m_isConnected(FALSE),
	m_xorKey1(0),
	m_xorKey2(0)
{
	m_vtcp.MemLoadLibrary(mem_vtcp,sizeof(mem_vtcp));

	m_vsend = (_vtcp_send)m_vtcp.MemGetProcAddress("vtcp_send");
	m_vrecv = (_vtcp_recv)m_vtcp.MemGetProcAddress("vtcp_recv");
	m_vsocket = (_vtcp_socket)m_vtcp.MemGetProcAddress("vtcp_socket");
	m_vconnect = (_vtcp_connect)m_vtcp.MemGetProcAddress("vtcp_connect");
	m_vstartup = (_vtcp_startup)m_vtcp.MemGetProcAddress("vtcp_startup");
	m_vclose = (_vtcp_close)m_vtcp.MemGetProcAddress("vtcp_close");

	m_vstartup();

	if (isSecure)
	{
		srand(GetTickCount());
		m_xorKey1 = (BYTE)(rand() % 255);
		m_xorKey2 = (BYTE)(rand() % 255);

		m_isSecure = isSecure;
	}
}

UdpComm::~UdpComm(void)
{
	m_vclose(m_sock);
}

BOOL UdpComm::SendAll(VTCP_SOCKET s,LPCVOID lpBuf, int nBufLen)
{
	if (VTCP_INVALID_SOCKET == s) 
	{
		errorLog(_T("socket is invalid. send failed"));
		return FALSE;
	}

	char* p = (char*) lpBuf;
	int iLeft = nBufLen;
	int iSent = m_vsend(s, p, iLeft, 0);
	while (iSent > 0 && iSent < iLeft)
	{
		iLeft -= iSent;
		p += iSent;

		iSent = m_vsend(s, p, iLeft, 0);
	}

	return (iSent > 0);
}

BOOL UdpComm::ReceiveAll(VTCP_SOCKET s, LPCVOID lpBuf,int nBufLen)
{
	if (VTCP_INVALID_SOCKET == s) 
	{
		errorLog(_T("socket is invalid. recv failed"));
		return FALSE;
	}

	char* p = (char*) lpBuf;
	int iLeft = nBufLen;
	int iRecv = m_vrecv(s, p, iLeft, 0);
	while (iRecv > 0 && iRecv < iLeft)
	{
		iLeft -= iRecv;
		p += iRecv;

		iRecv = m_vrecv(s, p, iLeft, 0);
	}

	return (iRecv > 0);
}

BOOL UdpComm::Send( ULONG targetIP, const LPBYTE pData, DWORD dwSize )
{
	IN_ADDR addr;
	addr.S_un.S_addr = targetIP;

	ByteBuffer sendByteBuffer;
	sendByteBuffer.Alloc(dwSize);
	memcpy((LPBYTE)sendByteBuffer, pData, dwSize);

	if ( !m_isConnected )
		m_isConnected = Connect(targetIP, g_ConfigInfo.nPort);

	if ( m_isConnected )
		m_isConnected = SendAll(m_sock,(LPBYTE)sendByteBuffer,sendByteBuffer.Size());

	return m_isConnected;
}


BOOL UdpComm::SendAndRecv( ULONG targetIP, const LPBYTE pSendData, DWORD dwSendSize, LPBYTE* pRecvData, DWORD& dwRecvSize )
{
	UDP_HEADER sendHead;
	sendHead.flag = UDP_FLAG;
	sendHead.nSize = dwSendSize;

	BOOL ret = FALSE;

	do 
	{
		if (! Send( targetIP, (PBYTE)&sendHead, sizeof(UDP_HEADER))) break;

		if (m_isSecure)
			XFC(pSendData,dwSendSize,pSendData,m_xorKey1,m_xorKey2);

		if (! Send( targetIP, pSendData, dwSendSize)) break;

		UDP_HEADER recvHead = {0};

		if ( !ReceiveAll(m_sock,(char*)&recvHead, sizeof(UDP_HEADER)))
		{
			m_isConnected = FALSE;
			break;
		}


		ByteBuffer buffer;
		buffer.Alloc(recvHead.nSize);

		if (! ReceiveAll(m_sock,(LPBYTE)buffer,recvHead.nSize))
		{
			m_isConnected = FALSE;
			buffer.Free();
			break;
		}

		//��������
		*pRecvData = Alloc(recvHead.nSize);
		memcpy(*pRecvData, (LPBYTE)buffer, recvHead.nSize);
		dwRecvSize =  recvHead.nSize;

		if(m_isSecure)
			XFC(*pRecvData,recvHead.nSize,*pRecvData,m_xorKey1,m_xorKey2);

		buffer.Free();

		ret = TRUE;

	} while (FALSE);



	return ret;
}

BOOL UdpComm::Connect( ULONG targetIP,int port )
{
	SOCKADDR_IN hints;

	memset(&hints, 0, sizeof(SOCKADDR_IN));
	hints.sin_family = AF_INET;
	hints.sin_addr.s_addr = targetIP;
	hints.sin_port = htons(port);

	m_sock = m_vsocket(AF_INET,SOCK_DGRAM,0);

	if (VTCP_ERROR == m_vconnect(m_sock,(sockaddr*)&hints,sizeof(hints)))
	{
		return FALSE;
	}

	if (m_isSecure)
	{
		int key1 = 0;
		int key2 = 0;

		int flag = UDP_FLAG;

		SendAll(m_sock,(LPVOID)&flag,sizeof(int));

		ReceiveAll(m_sock,&m_rsaKey,sizeof(RSA::RSA_PUBLIC_KEY));

		RSA::RSAEncrypt((char*)&m_xorKey1,(int*)&key1,m_rsaKey.d,m_rsaKey.n,1);
		RSA::RSAEncrypt((char*)&m_xorKey2,(int*)&key2,m_rsaKey.d,m_rsaKey.n,1);

		SendAll(m_sock,&key1,sizeof(int));
		SendAll(m_sock,&key2,sizeof(int));
	}

	return TRUE;

}