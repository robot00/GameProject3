﻿#include "stdafx.h"
#include "ServiceBase.h"
#include "NetManager.h"
#include "CommonSocket.h"
#include "CommandDef.h"
#include "CommonEvent.h"
#include "CommonConvert.h"
#include "DataBuffer.h"
#include "Connection.h"
#include "CommonFunc.h"
#include "TimerManager.h"
#include "PacketHeader.h"
#include "Log.h"

#define NEW_CONNECTION 1
#define CLOSE_CONNECTION 2

ServiceBase::ServiceBase(void)
{
	m_pPacketDispatcher = NULL;
}

ServiceBase::~ServiceBase(void)
{
}

ServiceBase* ServiceBase::GetInstancePtr()
{
	static ServiceBase _ServiceBase;

	return &_ServiceBase;
}


BOOL ServiceBase::OnDataHandle(IDataBuffer* pDataBuffer, CConnection* pConnection)
{
	PacketHeader* pHeader = (PacketHeader*)pDataBuffer->GetBuffer();
	if(!m_DataQueue.push(NetPacket(pConnection->GetConnectionID(), pDataBuffer, pHeader->dwMsgID)))
	{
		//处理太慢，消息太多
	}
	return TRUE;
}

BOOL ServiceBase::StartNetwork(UINT16 nPortNum, UINT32 nMaxConn, IPacketDispatcher* pDispather)
{
	if (pDispather == NULL)
	{
		ASSERT_FAIELD;
		return FALSE;
	}

	if((nPortNum <= 0) || (nMaxConn <= 0))
	{
		ASSERT_FAIELD;
		return FALSE;
	}

	m_pPacketDispatcher = pDispather;

	if (!CNetManager::GetInstancePtr()->Start(nPortNum, nMaxConn, this))
	{
		CLog::GetInstancePtr()->LogError("启动网络层失败!");
		return FALSE;
	}

	m_dwLastTick = 0;
	m_dwRecvNum = 0;
	m_dwFps = 0;
	m_dwSendNum = 0;
	return TRUE;
}

BOOL ServiceBase::StopNetwork()
{
	CLog::GetInstancePtr()->LogError("==========服务器开始关闭=======================");

	CNetManager::GetInstancePtr()->Close();

	CLog::GetInstancePtr()->CloseLog();

	return TRUE;
}

template<typename T>
BOOL ServiceBase::SendMsgStruct(UINT32 dwConnID, UINT32 dwMsgID, UINT64 u64TargetID, UINT32 dwUserData, T& Data)
{
	ERROR_RETURN_FALSE(dwConnID != 0);

	m_dwSendNum++;
	return CNetManager::GetInstancePtr()->SendMessageByConnID(dwConnID, dwMsgID, u64TargetID, dwUserData, &Data, sizeof(T));
}

BOOL ServiceBase::SendMsgProtoBuf(UINT32 dwConnID, UINT32 dwMsgID, UINT64 u64TargetID, UINT32 dwUserData, const google::protobuf::Message& pdata)
{
	ERROR_RETURN_FALSE(dwConnID != 0);

	char szBuff[102400] = {0};

	ERROR_RETURN_FALSE(pdata.ByteSize() < 102400);

	pdata.SerializePartialToArray(szBuff, pdata.GetCachedSize());
	m_dwSendNum++;
	return CNetManager::GetInstancePtr()->SendMessageByConnID(dwConnID, dwMsgID, u64TargetID, dwUserData, szBuff, pdata.GetCachedSize());
}

BOOL ServiceBase::SendMsgRawData(UINT32 dwConnID, UINT32 dwMsgID, UINT64 u64TargetID, UINT32 dwUserData, const char* pdata, UINT32 dwLen)
{
	ERROR_RETURN_FALSE(dwConnID != 0);
	m_dwSendNum++;
	return CNetManager::GetInstancePtr()->SendMessageByConnID(dwConnID, dwMsgID, u64TargetID, dwUserData, pdata, dwLen);
}

BOOL ServiceBase::SendMsgBuffer(UINT32 dwConnID, IDataBuffer* pDataBuffer)
{
	m_dwSendNum++;
	return CNetManager::GetInstancePtr()->SendMsgBufByConnID(dwConnID, pDataBuffer);
}

CConnection* ServiceBase::ConnectToOtherSvr( std::string strIpAddr, UINT16 sPort )
{
	if(strIpAddr.empty() || sPort <= 0)
	{
		ASSERT_FAIELD;
		return NULL;
	}

	return CNetManager::GetInstancePtr()->ConnectToOtherSvrEx(strIpAddr, sPort);
}

BOOL ServiceBase::OnCloseConnect( CConnection* pConnection )
{
	ERROR_RETURN_FALSE(pConnection->GetConnectionID() != 0);

	m_DataQueue.push(NetPacket(pConnection->GetConnectionID(), (IDataBuffer*)pConnection, CLOSE_CONNECTION));

	return TRUE;
}

BOOL ServiceBase::OnNewConnect( CConnection* pConnection )
{
	ERROR_RETURN_FALSE(pConnection->GetConnectionID() != 0);

	m_DataQueue.push(NetPacket(pConnection->GetConnectionID(), (IDataBuffer*)pConnection, NEW_CONNECTION));

	return TRUE;
}


CConnection* ServiceBase::GetConnectionByID( UINT32 dwConnID )
{
	return CConnectionMgr::GetInstancePtr()->GetConnectionByConnID(dwConnID);
}

BOOL ServiceBase::Update()
{
	if(m_dwLastTick == 0)
	{
		m_dwLastTick = CommonFunc::GetTickCount();
	}

	CConnectionMgr::GetInstancePtr()->CheckConntionAvalible();

	//处理新连接的通知
	NetPacket item;
	while(m_DataQueue.pop(item))
	{
		if (item.m_dwMsgID == NEW_CONNECTION)
		{
			m_pPacketDispatcher->OnNewConnect((CConnection*)item.m_pDataBuffer);
		}
		else if (item.m_dwMsgID == CLOSE_CONNECTION)
		{
			m_pPacketDispatcher->OnCloseConnect((CConnection*)item.m_pDataBuffer);
			//发送通知
			CConnectionMgr::GetInstancePtr()->DeleteConnection((CConnection*)item.m_pDataBuffer);
		}
		else
		{
			m_pPacketDispatcher->DispatchPacket(&item);

			item.m_pDataBuffer->Release();

			m_dwRecvNum += 1;
		}
	}

	m_dwFps += 1;

	if((CommonFunc::GetTickCount() - m_dwLastTick) > 1000)
	{
		m_pPacketDispatcher->OnSecondTimer();

		CLog::GetInstancePtr()->SetTitle("[FPS:%d]-[RecvNum:%d]--[SendNum:%d]", m_dwFps, m_dwRecvNum, m_dwSendNum);
		m_dwFps = 0;
		m_dwRecvNum = 0;
		m_dwSendNum = 0;
		m_dwLastTick = CommonFunc::GetTickCount();
	}

	TimerManager::GetInstancePtr()->UpdateTimer();

	CLog::GetInstancePtr()->Flush();

	return TRUE;
}
