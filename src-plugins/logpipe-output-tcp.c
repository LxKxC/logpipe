#include "logpipe_api.h"

/* communication protocol :
	|'@'(1byte)|filename_len(2bytes)|file_name|file_block_len(2bytes)|file_block_data|...(other file blocks)...|\0\0\0\0|
*/

char	*__LOGPIPE_OUTPUT_TCP_VERSION = "0.1.0" ;

struct ForwardSession
{
	char			*ip ;
	int			port ;
	struct sockaddr_in   	addr ;
	int			sock ;
} ;

#define IP_PORT_MAXCNT		8

struct OutputPluginContext
{
	struct ForwardSession	forward_session_array[IP_PORT_MAXCNT] ;
	int			forward_session_count ;
	struct ForwardSession	*p_forward_session ;
	int			forward_session_index ;
} ;

funcLoadOutputPluginConfig LoadOutputPluginConfig ;
int LoadOutputPluginConfig( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , struct LogpipePluginConfigItem *p_plugin_config_items , void **pp_context )
{
	struct OutputPluginContext	*p_plugin_ctx = NULL ;
	int				i ;
	struct ForwardSession		*p_forward_session = NULL ;
	char				*p = NULL ;
	
	/* �����ڴ��Դ�Ų�������� */
	p_plugin_ctx = (struct OutputPluginContext *)malloc( sizeof(struct OutputPluginContext) ) ;
	if( p_plugin_ctx == NULL )
	{
		ERRORLOG( "malloc failed , errno[%d]" , errno );
		return -1;
	}
	memset( p_plugin_ctx , 0x00 , sizeof(struct OutputPluginContext) );
	
	/* ����������� */
	for( i = 0 , p_forward_session = p_plugin_ctx->forward_session_array ; i < IP_PORT_MAXCNT ; i++ , p_forward_session++ )
	{
		if( i == 0 )
			p_forward_session->ip = QueryPluginConfigItem( p_plugin_config_items , "ip" ) ;
		else
			p_forward_session->ip = QueryPluginConfigItem( p_plugin_config_items , "ip%d" , i ) ;
		INFOLOG( "ip%d[%s]" , p_forward_session->ip )
		if( p_forward_session->ip == NULL || p_forward_session->ip[0] == '\0' )
			break;
		
		if( i == 0 )
			p = QueryPluginConfigItem( p_plugin_config_items , "port" ) ;
		else
			p = QueryPluginConfigItem( p_plugin_config_items , "port%d" , i ) ;
		if( p == NULL || p[0] == '\0' )
		{
			ERRORLOG( "expect config for 'port'" );
			return -1;
		}
		p_forward_session->port = atoi(p) ;
		INFOLOG( "port%d[%d]" , p_forward_session->port )
		if( p_forward_session->port <= 0 )
		{
			ERRORLOG( "port[%s] invalid" , p );
			return -1;
		}
		
		p_plugin_ctx->forward_session_count++;
	}
	if( p_plugin_ctx->forward_session_count == 0 )
	{
		ERRORLOG( "expect config for 'ip'" );
		return -1;
	}
	
	/* ���ò������������ */
	(*pp_context) = p_plugin_ctx ;
	
	return 0;
}

static int CheckAndConnectForwardSocket( struct OutputPluginContext *p_plugin_ctx , int forward_session_index )
{
	struct ForwardSession	*p_forward_session = NULL ;
	
	int			nret = 0 ;
	
	while(1)
	{
		if( forward_session_index >= 0 )
		{
			p_forward_session = p_plugin_ctx->forward_session_array + forward_session_index ;
		}
		else
		{
			p_plugin_ctx->forward_session_index++;
			if( p_plugin_ctx->forward_session_index >= IP_PORT_MAXCNT )
				p_plugin_ctx->forward_session_index = 0 ;
			p_plugin_ctx->p_forward_session = p_forward_session = p_plugin_ctx->forward_session_array + p_plugin_ctx->forward_session_index ;
		}
		
		if( p_forward_session->sock >= 0 )
			return 0;
		
		/* �����׽��� */
		p_forward_session->sock = socket( AF_INET , SOCK_STREAM , IPPROTO_TCP ) ;
		if( p_forward_session->sock == -1 )
		{
			ERRORLOG( "socket failed , errno[%d]" , errno );
			return -1;
		}
		
		/* �����׽���ѡ�� */
		{
			int	onoff = 1 ;
			setsockopt( p_forward_session->sock , SOL_SOCKET , SO_REUSEADDR , (void *) & onoff , sizeof(int) );
		}
		
		{
			int	onoff = 1 ;
			setsockopt( p_forward_session->sock , IPPROTO_TCP , TCP_NODELAY , (void*) & onoff , sizeof(int) );
		}
		
		/* ���ӵ�����������˿� */
		nret = connect( p_forward_session->sock , (struct sockaddr *) & (p_forward_session->addr) , sizeof(struct sockaddr) ) ;
		if( nret == -1 )
		{
			ERRORLOG( "connect[%s:%d][%d] failed , errno[%d]" , p_forward_session->ip , p_forward_session->port , p_forward_session->sock , errno );
			close( p_forward_session->sock ); p_forward_session->sock = -1 ;
			sleep(1);
			if( forward_session_index < 0 )
			{
				p_plugin_ctx->forward_session_index++;
				if( p_plugin_ctx->forward_session_index >= IP_PORT_MAXCNT )
					p_plugin_ctx->forward_session_index = 0 ;
			}
		}
		else
		{
			INFOLOG( "connect[%s:%d][%d] ok" , p_forward_session->ip , p_forward_session->port , p_forward_session->sock );
			return 0;
		}
	}
}

funcInitOutputPluginContext InitOutputPluginContext ;
int InitOutputPluginContext( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , void *p_context )
{
	struct OutputPluginContext	*p_plugin_ctx = (struct OutputPluginContext *)p_context ;
	
	int				i ;
	struct ForwardSession		*p_forward_session = NULL ;
	
	int				nret = 0 ;
	
	/* ��ʼ����������ڲ����� */
	for( i = 0 , p_forward_session = p_plugin_ctx->forward_session_array ; i < p_plugin_ctx->forward_session_count ; i++ , p_forward_session++ )
	{
		memset( & (p_forward_session->addr) , 0x00 , sizeof(struct sockaddr_in) );
		p_forward_session->addr.sin_family = AF_INET ;
		if( p_forward_session->ip[0] == '\0' )
			p_forward_session->addr.sin_addr.s_addr = INADDR_ANY ;
		else
			p_forward_session->addr.sin_addr.s_addr = inet_addr(p_forward_session->ip) ;
		p_forward_session->addr.sin_port = htons( (unsigned short)(p_forward_session->port) );
		
		/* ���ӷ���� */
		p_forward_session->sock = -1 ;
		nret = CheckAndConnectForwardSocket( p_plugin_ctx , i ) ;
		if( nret )
			return -1;
		
		/* �������������� */
		AddOutputPluginEvent( p_env , p_logpipe_output_plugin , p_forward_session->sock , p_forward_session );
	}
	
	p_plugin_ctx->forward_session_index = 0 ;
	
	return 0;
}

funcOnOutputPluginEvent OnOutputPluginEvent;
int OnOutputPluginEvent( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , void *p_context )
{
	struct OutputPluginContext	*p_plugin_ctx = (struct OutputPluginContext *)p_context ;
	
	int				i ;
	struct ForwardSession		*p_forward_session = NULL ;
	
	int				nret = 0 ;
	
	for( i = 0 , p_forward_session = p_plugin_ctx->forward_session_array ; i < p_plugin_ctx->forward_session_count ; i++ , p_forward_session++ )
	{
		if( p_forward_session == p_context )
		{
			/* �ر����� */
			DeleteOutputPluginEvent( p_env , p_logpipe_output_plugin , p_forward_session->sock );
			ERRORLOG( "remote socket closed , close forward sock[%d]" , p_forward_session->sock )
			close( p_forward_session->sock ); p_forward_session->sock = -1 ;
			
			/* ���ӷ���� */
			sleep(1);
			nret = CheckAndConnectForwardSocket( p_plugin_ctx , p_forward_session - p_plugin_ctx->forward_session_array ) ;
			if( nret < 0 )
				return nret;
			
			/* �������������� */
			AddOutputPluginEvent( p_env , p_logpipe_output_plugin , p_forward_session->sock , p_forward_session );
		}
	}
	
	return 0;
}

funcBeforeWriteOutputPlugin BeforeWriteOutputPlugin ;
int BeforeWriteOutputPlugin( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , void *p_context , uint16_t filename_len , char *filename )
{
	struct OutputPluginContext	*p_plugin_ctx = (struct OutputPluginContext *)p_context ;
	
	uint16_t			*filename_len_htons = NULL ;
	char				comm_buf[ 1 + sizeof(uint16_t) + PATH_MAX ] ;
	int				len ;
	
	int				nret = 0 ;
	
_GOTO_RETRY_SEND :
	
	nret = CheckAndConnectForwardSocket( p_plugin_ctx , -1 ) ;
	if( nret < 0 )
		return nret;
	
	memset( comm_buf , 0x00 , sizeof(comm_buf) );
	comm_buf[0] = LOGPIPE_COMM_HEAD_MAGIC ;
	
	if( filename_len > PATH_MAX )
	{
		ERRORLOG( "filename length[%d] too long" , filename_len )
		return 1;
	}
	
	filename_len_htons = (uint16_t*)(comm_buf+1) ;
	(*filename_len_htons) = htons(filename_len) ;
	
	strncpy( comm_buf+1+sizeof(uint16_t) , filename , filename_len );
	
	/* ����ͨѶͷ���ļ��� */
	len = writen( p_plugin_ctx->p_forward_session->sock , comm_buf , 1+sizeof(uint16_t)+filename_len ) ;
	if( len == -1 )
	{
		ERRORLOG( "send comm magic and filename failed , errno[%d]" , errno )
		close( p_plugin_ctx->p_forward_session->sock ); p_plugin_ctx->p_forward_session->sock = -1 ;
		goto _GOTO_RETRY_SEND;
	}
	else
	{
		INFOLOG( "send comm magic and filename ok , [%d]bytes" , 1+sizeof(uint16_t)+filename_len )
		DEBUGHEXLOG( comm_buf , len , NULL )
	}
	
	return 0;
}

funcWriteOutputPlugin WriteOutputPlugin ;
int WriteOutputPlugin( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , void *p_context , uint32_t block_len , char *block_buf )
{
	struct OutputPluginContext	*p_plugin_ctx = (struct OutputPluginContext *)p_context ;
	
	uint32_t			block_len_htonl ;
	
	int				len ;
	
	block_len_htonl = htonl(block_len) ;
	len = writen( p_plugin_ctx->p_forward_session->sock , & block_len_htonl , sizeof(block_len_htonl) ) ;
	if( len == -1 )
	{
		ERRORLOG( "send block len to socket failed , errno[%d]" , errno )
		close( p_plugin_ctx->p_forward_session->sock ); p_plugin_ctx->p_forward_session->sock = -1 ;
		return 1;
	}
	else
	{
		INFOLOG( "send block len to socket ok , [%d]bytes" , sizeof(block_len_htonl) )
		DEBUGHEXLOG( (char*) & block_len_htonl , len , NULL )
	}
	
	len = writen( p_plugin_ctx->p_forward_session->sock , block_buf , block_len ) ;
	if( len == -1 )
	{
		ERRORLOG( "send block data to socket failed , errno[%d]" , errno )
		close( p_plugin_ctx->p_forward_session->sock ); p_plugin_ctx->p_forward_session->sock = -1 ;
		return 1;
	}
	else
	{
		INFOLOG( "send block data to socket ok , [%d]bytes" , block_len )
		DEBUGHEXLOG( block_buf , len , NULL )
	}
	
	return 0;
}

funcAfterWriteOutputPlugin AfterWriteOutputPlugin ;
int AfterWriteOutputPlugin( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , void *p_context , uint16_t filename_len , char *filename )
{
	struct OutputPluginContext	*p_plugin_ctx = (struct OutputPluginContext *)p_context ;
	
	uint32_t			block_len_htonl ;
	
	int				len ;
	
	block_len_htonl = htonl(0) ;
	len = writen( p_plugin_ctx->p_forward_session->sock , & block_len_htonl , sizeof(block_len_htonl) ) ;
	if( len == -1 )
	{
		ERRORLOG( "send block len to socket failed , errno[%d]" , errno )
		close( p_plugin_ctx->p_forward_session->sock ); p_plugin_ctx->p_forward_session->sock = -1 ;
		return 1;
	}
	else
	{
		INFOLOG( "send block len to socket ok , [%d]bytes" , sizeof(block_len_htonl) )
		DEBUGHEXLOG( (char*) & block_len_htonl , len , NULL )
	}
	
	return 0;
}

funcCleanOutputPluginContext CleanOutputPluginContext ;
int CleanOutputPluginContext( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , void *p_context )
{
	struct OutputPluginContext	*p_plugin_ctx = (struct OutputPluginContext *)p_context ;
	
	if( p_plugin_ctx->p_forward_session->sock >= 0 )
	{
		INFOLOG( "close forward sock[%d]" , p_plugin_ctx->p_forward_session->sock )
		close( p_plugin_ctx->p_forward_session->sock ); p_plugin_ctx->p_forward_session->sock = -1 ;
	}
	
	return 0;
}

funcUnloadOutputPluginConfig UnloadOutputPluginConfig ;
int UnloadOutputPluginConfig( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , void **pp_context )
{
	struct OutputPluginContext	**pp_plugin_ctx = (struct OutputPluginContext **)pp_context ;
	
	/* �ͷ��ڴ��Դ�Ų�������� */
	free( (*pp_plugin_ctx) ); (*pp_plugin_ctx) = NULL ;
	
	return 0;
}

