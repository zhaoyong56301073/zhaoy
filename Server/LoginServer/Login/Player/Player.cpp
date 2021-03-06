#include "stdafx.h"

#include "Player.h"
#include "Socket.h"
#include "PacketFactoryManager.h"
#include "TimeManager.h"
#include "Log.h"


using namespace Packets ;

Player::Player( BOOL bIsServer )
{
__ENTER_FUNCTION

	m_PID = INVALID_ID ;
	m_UID = INVALID_ID ;
	m_PlayerManagerID = INVALID_ID ;


	m_pSocket = new Socket ;
	Assert( m_pSocket ) ;

	if( bIsServer )
	{
		m_pSocketInputStream = new SocketInputStream( m_pSocket,DEFAULTSOCKETINPUTBUFFERSIZE,64*1024*1024 ) ;
		Assert( m_pSocketInputStream ) ;

		m_pSocketOutputStream = new SocketOutputStream( m_pSocket,DEFAULTSOCKETOUTPUTBUFFERSIZE,64*1024*1024 ) ;
		Assert( m_pSocketOutputStream ) ;
	}
	else
	{
		m_pSocketInputStream = new SocketInputStream( m_pSocket ) ;
		Assert( m_pSocketInputStream ) ;

		m_pSocketOutputStream = new SocketOutputStream( m_pSocket ) ;
		Assert( m_pSocketOutputStream ) ;
	}

	m_IsEmpty		= TRUE ;
	m_IsDisconnect	= FALSE ;

	m_PacketIndex	 = 0 ;

__LEAVE_FUNCTION
}

Player::~Player( )
{
__ENTER_FUNCTION

	SAFE_DELETE( m_pSocketInputStream ) ;
	SAFE_DELETE( m_pSocketOutputStream ) ;

	SAFE_DELETE( m_pSocket ) ;

__LEAVE_FUNCTION
}

VOID Player::CleanUp( )
{
__ENTER_FUNCTION

	m_pSocket->close() ;
	m_pSocketInputStream->CleanUp() ;
	m_pSocketOutputStream->CleanUp() ;
	SetPlayerManagerID( INVALID_ID ) ;
	SetUserID( INVALID_ID ) ;
	m_PacketIndex = 0 ;
	SetDisconnect(FALSE) ;


__LEAVE_FUNCTION
}

VOID Player::Disconnect( )
{
__ENTER_FUNCTION

	_MY_TRY
	{
		m_pSocket->close() ;
	}
	_MY_CATCH
	{
	}

__LEAVE_FUNCTION
}

BOOL Player::IsValid( )
{
__ENTER_FUNCTION

	if( m_pSocket==NULL ) return FALSE ;

	if( !m_pSocket->isValid() ) return FALSE ;


	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}

BOOL Player::ProcessInput( )
{
__ENTER_FUNCTION

	if( IsDisconnect() )
		return TRUE ;

	_MY_TRY 
	{
		uint ret = m_pSocketInputStream->Fill( ) ;
		if( (INT)ret <= SOCKET_ERROR )
		{
			Log::SaveLog( "./Log/LoginError.log", "[%d] m_pSocketInputStream->Fill ret:%d %s", 
				g_pTimeManager->Time2DWORD(), (INT)ret, MySocketError() ) ;
			return FALSE ;
		}
	} 
	_MY_CATCH
	{
		return FALSE ;
	}


	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}

BOOL Player::ProcessCommand( BOOL Option )
{
__ENTER_FUNCTION

	BOOL ret ;

	CHAR header[PACKET_HEADER_SIZE];
	PacketID_t packetID;
	uint packetuint, packetSize, packetIndex;
	WORD packetTick;
	Packet* pPacket = NULL ;

	if( IsDisconnect( ) )
		return TRUE ;

	_MY_TRY
	{
		if( Option ) 
		{//????????????????
		}

		for( ;; )
		{
			if( !m_pSocketInputStream->Peek(&header[0], PACKET_HEADER_SIZE) )
			{//??????????????????
				break ;
			}

			this->DecryptHead_CS(header);//??????????

			memcpy( &packetID, &header[0], sizeof(PacketID_t) ) ;	
			memcpy( &packetTick, &header[sizeof(WORD)], sizeof(WORD) ) ;
			memcpy( &packetuint, &header[sizeof(WORD)+sizeof(PacketID_t)], sizeof(uint) );
			packetSize = GET_PACKET_LEN(packetuint) ;
			packetIndex = GET_PACKET_INDEX(packetuint) ;

			if( packetID >= (PacketID_t)PACKET_MAX )
			{//??????????????
				Assert( FALSE ) ;
				return FALSE ;
			}

			//????????????--Begin
			{
				UINT t_uSize = packetSize+PACKET_HEADER_SIZE;
				UINT t_uHead = m_pSocketInputStream->GetHead();
				UINT t_uTail = m_pSocketInputStream->GetTail();
				UINT t_uBufferLen = m_pSocketInputStream->GetBuffLen();
				CHAR* t_szBuffer = m_pSocketInputStream->GetBuff();
				if ( t_uHead < t_uTail ) 
				{
					this->Decrypt_CS(&t_szBuffer[t_uHead], t_uSize, 0);
				} 
				else 
				{
					UINT rightLen = t_uBufferLen - t_uHead ;
					if( t_uSize <= rightLen ) 
					{
						this->Decrypt_CS(&t_szBuffer[t_uHead], t_uSize, 0);
					} 
					else 
					{
						this->Decrypt_CS(&t_szBuffer[t_uHead], rightLen, 0);
						this->Decrypt_CS(t_szBuffer, t_uSize-rightLen, rightLen);
					}
				}
			}
			//????????????--End

			_MY_TRY
			{

				if( m_pSocketInputStream->Length()<PACKET_HEADER_SIZE+packetSize )
				{//??????????????
					break;
				}

				if( packetSize>g_pPacketFactoryManager->GetPacketMaxSize(packetID) )
				{//????????????????????????????????????????????????????????
					Assert( FALSE ) ;
//					m_pSocketInputStream->Skip( PACKET_HEADER_SIZE+packetSize ) ;
					return FALSE ;
				}

				Packet* pPacket = g_pPacketFactoryManager->CreatePacket( packetID ) ;
				if( pPacket==NULL )
				{//????????????????????
					Assert( FALSE ) ;
//					m_pSocketInputStream->Skip( PACKET_HEADER_SIZE+packetSize ) ;
					return FALSE ;
				}

				//??????????????
				pPacket->SetPacketIndex( packetIndex ) ;

				ret = m_pSocketInputStream->ReadPacket( pPacket ) ;
				if( ret==FALSE )
				{//????????????????
					Assert( FALSE ) ;
					g_pPacketFactoryManager->RemovePacket( pPacket ) ;
					return FALSE ;
				}

				BOOL bNeedRemove = TRUE ;

				_MY_TRY
				{
					//????m_KickTime??????m_KickTime????????????????????????????
					//????????????
					this->ResetKick( ) ;

					uint uret = pPacket->Execute( this ) ;
					if( uret==PACKET_EXE_ERROR )
					{//????????????????????????????
						if( pPacket ) 
							g_pPacketFactoryManager->RemovePacket( pPacket ) ;
						return FALSE ;
					}
					else if( uret==PACKET_EXE_BREAK )
					{//????????????????????????
					 //??????????????????????????????????????????????
					 //??????????
					 //????????????????????????????????????????????????????
					 //??????????????????????????????????????????
						if( pPacket ) 
							g_pPacketFactoryManager->RemovePacket( pPacket ) ;
						break ;
					}
					else if( uret==PACKET_EXE_CONTINUE )
					{//??????????????????
					}
					else if( uret==PACKET_EXE_NOTREMOVE )
					{//??????????????????????????????????????
						bNeedRemove = FALSE ;
					}
					else if( uret == PACKET_EXE_NOTREMOVE_ERROR )
					{
						return FALSE ;
					}
					else
					{//????????????
						Assert(FALSE) ;
					}
				}
				_MY_CATCH
				{
				}

				if( pPacket && bNeedRemove ) 
					g_pPacketFactoryManager->RemovePacket( pPacket ) ;
			}
			_MY_CATCH
			{
				return FALSE;
			}
		}
	}
	_MY_CATCH
	{
	}



	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}

BOOL Player::ProcessOutput( )
{
__ENTER_FUNCTION

	if( IsDisconnect( ) )
		return TRUE ;

	_MY_TRY
	{
		uint size = m_pSocketOutputStream->Length() ;
		if( size==0 )
		{
			return TRUE ;
		}

		uint ret = m_pSocketOutputStream->Flush( ) ;
		if( (INT)ret <= SOCKET_ERROR )
		{
			Log::SaveLog( "./Log/LoginError.log", "[%d] m_pSocketOutputStream->Flush ret:%d %s", 
				g_pTimeManager->Time2DWORD(), (INT)ret, MySocketError() ) ;
			return FALSE ;
		}
	} 
	_MY_CATCH
	{
		return FALSE ;
	}

	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}


BOOL Player::SendPacket( Packet* pPacket )
{
__ENTER_FUNCTION

	if( IsDisconnect( ) )
		return TRUE ;

	if( m_pSocketOutputStream!=NULL )
	{
		pPacket->SetPacketIndex( m_PacketIndex++ ) ;

		PacketID_t packetID = pPacket->GetPacketID() ;
		UINT w;
		if ( packetID < PACKET_LC_MAXCOUNT)
		{
			w = m_pSocketOutputStream->Write( PACK_COMPART , PACK_COMPART_SIZE ) ;
		}

		UINT t_uTail_Begin = m_pSocketOutputStream->GetTail();//????????????????????????????????

		w = m_pSocketOutputStream->Write( (CHAR*)&packetID , sizeof(PacketID_t) ) ;

		WORD packetTick = g_pTimeManager->RunTick();
		w = m_pSocketOutputStream->Write( (CHAR*)&packetTick , sizeof(WORD) ) ;

		UINT packetUINT ;
		UINT packetSize = pPacket->GetPacketSize( ) ;
		UINT packetIndex = pPacket->GetPacketIndex( ) ;

		SET_PACKET_INDEX(packetUINT, packetIndex) ;
		SET_PACKET_LEN(packetUINT, packetSize) ;

		w = m_pSocketOutputStream->Write( (CHAR*)&packetUINT, sizeof(UINT) ) ;

		BOOL ret = pPacket->Write( *m_pSocketOutputStream ) ;

		UINT t_uTail_End = m_pSocketOutputStream->GetTail();//????????????????????????????????

		//????????????--Begin
		{
			UINT t_uSize = t_uTail_End - t_uTail_Begin;
			UINT t_uHead = m_pSocketOutputStream->GetHead();
			UINT t_uTail = m_pSocketOutputStream->GetTail();
			UINT t_uBufferLen = m_pSocketOutputStream->GetBuffLen();
			CHAR* t_szBuffer = m_pSocketOutputStream->GetBuff();
			if ( t_uHead < t_uTail ) 
			{
				this->Encrypt_SC(&(t_szBuffer[t_uTail_Begin]), t_uSize, 0);
			} 
			else
			{
				UINT rightLen = t_uBufferLen - t_uHead ;
				if( t_uSize <= rightLen ) 
				{
					this->Encrypt_SC(&(t_szBuffer[t_uTail_Begin]), t_uSize, 0);
				} 
				else 
				{
					this->Encrypt_SC(&(t_szBuffer[t_uTail_Begin]), rightLen, 0);
					this->Encrypt_SC(t_szBuffer, t_uSize-rightLen, rightLen);
				}
			}
		}
		//????????????--End

		//BOOL ret = m_pSocketOutputStream->WritePacket( pPacket ) ;
		Assert( ret ) ;
		//Log::SaveLog( "./Log/LoginDebug.log", "SendPacket! SOCKET=%d, ID=%d", 
		//	m_pSocket->getSOCKET(), pPacket->GetPacketID() ) ;
		
	}

	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}

BOOL Player::HeartBeat( UINT uTime )
{
__ENTER_FUNCTION



	return TRUE ;

__LEAVE_FUNCTION

	return FALSE ;
}

VOID Player::ResetKick( )
{
__ENTER_FUNCTION
__LEAVE_FUNCTION
}
//INT Player::WhereThisPlayerFrom(VOID) const
//{
//	if(NULL!=m_pSocket)
//	{
//		return g_Config.m_InternalIpofProxy.WhereThisIpFrom(m_pSocket->m_Host);
//	}
//	return INVALID_ISP;
//}
//CHAR* ProxyTool_GetCorrespondingIP(Player const* pPlayer, ID_t ServerID)
//{
//	__ENTER_FUNCTION
//	_SERVER_DATA* pData = g_pServerManager->FindServerInfo( ServerID );
//	if(NULL==pData)
//	{
//		CHAR szLog[1024];
//		tsnprintf(szLog, sizeof(szLog), "[ProxyTool_GetCorrespondingIP] Error: Can't found the specific server(%d).", ServerID);
//		szLog[sizeof(szLog)-1] = '\0';
//		AssertEx(pData, szLog);
//	}
//	INT nIsp = pPlayer->WhereThisPlayerFrom();
//	if(INVALID_ISP==nIsp)
//	{
//		return pData->m_IP0;
//	}
//	else
//	{
//		if(TRUE==RangeCheckForIndex_Assert(nIsp, 0, NUM_OF_ISP-1, "ProxyTool_GetCorrespondingIP"))
//		{
//			PROXY_DATA& rProxy = pData->m_aProxy[nIsp];
//			if(TRUE==rProxy.m_bEnabled)
//			{
//				return rProxy.m_szIP;
//			}
//			return pData->m_IP0;
//		}
//		else
//		{
//			return pData->m_IP0;
//		}
//	}
//	__LEAVE_FUNCTION
//	return NULL;
//}
//UINT  ProxyTool_GetCorrespondingPort(Player const* pPlayer, ID_t ServerID)
//{
//	__ENTER_FUNCTION
//	_SERVER_DATA* pData = g_pServerManager->FindServerInfo( ServerID );
//	if(NULL==pData)
//	{
//		CHAR szLog[1024];
//		tsnprintf(szLog, sizeof(szLog), "[ProxyTool_GetCorrespondingPort] Error: Can't found the specific server(%d).", ServerID);
//		szLog[sizeof(szLog)-1] = '\0';
//		AssertEx(pData, szLog);
//	}
//	INT nIsp = pPlayer->WhereThisPlayerFrom();
//	if(INVALID_ISP==nIsp)
//	{
//		return pData->m_Port0;
//	}
//	else
//	{
//		if(TRUE==RangeCheckForIndex_Assert(nIsp, 0, NUM_OF_ISP-1, "ProxyTool_GetCorrespondingIP"))
//		{
//			PROXY_DATA& rProxy = pData->m_aProxy[nIsp];
//			if(TRUE==rProxy.m_bEnabled)
//			{
//				return rProxy.m_nPort;
//			}
//			return pData->m_Port0;
//		}
//		else
//		{
//			return pData->m_Port0;
//		}
//	}
//	__LEAVE_FUNCTION
//	return NULL;
//}

