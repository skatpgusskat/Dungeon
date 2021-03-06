#include "stdafx.h"
#include "DungeonServer.h"
#include "..\..\PacketType.h"
#include "ThreadLocal.h"
#include "ClientSession.h"
#include "ClientManager.h"
#include "DatabaseJobContext.h"
#include "DatabaseJobManager.h"

ClientManager* GClientManager = nullptr;

ClientSession* ClientManager::CreateClient(SOCKET sock)
{
	assert(LThreadType == THREAD_CLIENT);

	ClientSession* client = new ClientSession(sock);
	mClientList.insert(ClientList::value_type(sock, client));

	return client;
}



void ClientManager::BroadcastPacket(ClientSession* from, PacketHeader* pkt)
{
	///FYI: C++ STL iterator 스타일의 루프
	for (ClientList::const_iterator it = mClientList.begin(); it != mClientList.end(); ++it)
	{
		ClientSession* client = it->second;

		if (from == client)
			continue;

		client->SendRequest(pkt);
	}
}

void ClientManager::OnPeriodWork()
{
	/// 접속이 끊긴 세션들 주기적으로 정리 (1초 정도 마다 해주자)
	DWORD currTick = GetTickCount();
	if (currTick - mLastGCTick >= 1000)
	{
		CollectGarbageSessions();
		mLastGCTick = currTick;
	}

	/// 처리 완료된 DB 작업들 각각의 Client로 dispatch
	DispatchDatabaseJobResults();
}

void ClientManager::CollectGarbageSessions()
{
	std::vector<ClientSession*> disconnectedSessions;

	///FYI: C++ 11 람다를 이용한 스타일
	std::for_each(mClientList.begin(), mClientList.end(),
		[&](ClientList::const_reference it)
	{
		ClientSession* client = it.second;

		if (false == client->IsConnected() && 0 == client->GetRefCount())
			disconnectedSessions.push_back(client);
	}
	);


	///FYI: C언어 스타일의 루프
	for (size_t i = 0; i < disconnectedSessions.size(); ++i)
	{
		ClientSession* client = disconnectedSessions[i];
		mClientList.erase(client->mSocket);
		delete client;
	}

}

void ClientManager::DispatchDatabaseJobResults()
{
	/// 쌓여 있는 DB 작업 처리 결과들을 각각의 클라에게 넘긴다
	DatabaseJobContext* dbResult = nullptr;
	while (GDatabaseJobManager->PopDatabaseJobResult(dbResult))
	{
			/// 여기는 해당 DB요청을 했던 클라이언트에서 직접 해줘야 는 경우다
		auto& it = mClientList.find(dbResult->mSockKey);
		if (false == dbResult->mSuccess)
		{
			printf("DB JOB FAIL \n");
		}
		else
		{
			if (it != mClientList.end() && it->second->IsConnected())
			{
				/// dispatch here....
				it->second->DatabaseJobDone(dbResult);
			}
		}

		/// 완료된 DB 작업 컨텍스트는 삭제해주자
		DatabaseJobContext* toBeDelete = dbResult;
		delete toBeDelete;
	}
}

void ClientManager::FlushClientSend()
{
	for (auto& it : mClientList)
	{
		ClientSession* client = it.second;
		if (false == client->SendFlush())
		{
			client->Disconnect();
		}
	}
}