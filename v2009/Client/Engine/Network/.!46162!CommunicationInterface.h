#ifndef SE_INCL_COMMUNICATIONINTERFACE_H
#define SE_INCL_COMMUNICATIONINTERFACE_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif



#define SERVER_CLIENTS 1
#define DEMO_CLIENT SERVER_CLIENTS

#include <Engine/Network/CPacket.h>
#include <Engine/Network/InputBuffer.h>
#include <Engine/Network/SocketCompat.h>

// Communication class
class ENGINE_API CCommunicationInterface 
{
public:
	// Login Server Information
	char	cci_szNick[100], cci_szAddr[100];
	int		cci_iPort;
	int		cci_iSelectedServerNum;

