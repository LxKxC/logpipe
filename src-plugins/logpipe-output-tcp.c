#include "logpipe_api.h"

/* communication protocol :
	|'@'(1byte)|filename_len(2bytes)|file_name|file_block_len(2bytes)|file_block_data|...(other file blocks)...|\0\0\0\0|
*/

char	*__LOGPIPE_OUTPUT_TCP_VERSION = "0.1.0" ;

struct OutputPluginContext
{
	char			*ip ;
	int			port ;
	
	struct sockaddr_in   	forward_addr ;
	int			forward_sock ;
} ;

funcLoadOutputPluginConfig LoadOutputPluginConfig ;
int LoadOutputPluginConfig( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , struct LogpipePluginConfigItem *p_plugin_config_items , void **pp_context )
{
	struct OutputPluginContext	*p_plugin_ctx = NULL ;
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
	p_plugin_ctx->ip = QueryPluginConfigItem( p_plugin_config_items , "ip" ) ;
	INFOLOG( "ip[%s]" , p_plugin_ctx->ip )
	if( p_plugin_ctx->ip == NULL || p_plugin_ctx->ip[0] == '\0' )
	{
		ERRORLOG( "expect config for 'ip'" );
		return -1;
	}
	
	p = QueryPluginConfigItem( p_plugin_config_items , "port" ) ;
	if( p == NULL || p[0] == '\0' )
	{
		ERRORLOG( "expect config for 'port'" );
		return -1;
	}
	p_plugin_ctx->port = atoi(p) ;
	INFOLOG( "port[%d]" , p_plugin_ctx->port )
	if( p_plugin_ctx->port <= 0 )
	{
		ERRORLOG( "port[%s] invalid" , p );
		return -1;
	}
	
	/* ���ò������������ */
	(*pp_context) = p_plugin_ctx ;
	
	return 0;
}

static int ConnectForwardSocket( struct OutputPluginContext *p_plugin_ctx )
{
	int		nret = 0 ;
	
	/* �����׽��� */
	p_plugin_ctx->forward_sock = socket( AF_INET , SOCK_STREAM , IPPROTO_TCP ) ;
	if( p_plugin_ctx->forward_sock == -1 )
	{
		ERRORLOG( "socket failed , errno[%d]" , errno );
		return 1;
	}
	
	/* �����׽���ѡ�� */
	{
		int	onoff = 1 ;
		setsockopt( p_plugin_ctx->forward_sock , SOL_SOCKET , SO_REUSEADDR , (void *) & onoff , sizeof(int) );
	}
	
	{
		int	onoff = 1 ;
		setsockopt( p_plugin_ctx->forward_sock , IPPROTO_TCP , TCP_NODELAY , (void*) & onoff , sizeof(int) );
	}
	
	/* ���ӵ�����������˿� */
	nret = connect( p_plugin_ctx->forward_sock , (struct sockaddr *) & (p_plugin_ctx->forward_addr) , sizeof(struct sockaddr) ) ;
	if( nret == -1 )
	{
		ERRORLOG( "connect[%s:%d][%d] failed , errno[%d]" , p_plugin_ctx->ip , p_plugin_ctx->port , p_plugin_ctx->forward_sock , errno );
		close( p_plugin_ctx->forward_sock ); p_plugin_ctx->forward_sock = -1 ;
		return 1;
	}
	else
	{
		INFOLOG( "connect[%s:%d][%d] ok" , p_plugin_ctx->ip , p_plugin_ctx->port , p_plugin_ctx->forward_sock );
	}
	
	return 0;
}

funcInitOutputPluginContext InitOutputPluginContext ;
int InitOutputPluginContext( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , void *p_context )
{
	struct OutputPluginContext	*p_plugin_ctx = (struct OutputPluginContext *)p_context ;
	
	int				nret = 0 ;
	
	/* ��ʼ����������ڲ����� */
	memset( & (p_plugin_ctx->forward_addr) , 0x00 , sizeof(struct sockaddr_in) );
	p_plugin_ctx->forward_addr.sin_family = AF_INET ;
	if( p_plugin_ctx->ip[0] == '\0' )
		p_plugin_ctx->forward_addr.sin_addr.s_addr = INADDR_ANY ;
	else
		p_plugin_ctx->forward_addr.sin_addr.s_addr = inet_addr(p_plugin_ctx->ip) ;
	p_plugin_ctx->forward_addr.sin_port = htons( (unsigned short)(p_plugin_ctx->port) );
	
	/* ���ӷ���� */
	p_plugin_ctx->forward_sock = -1 ;
	nret = ConnectForwardSocket( p_plugin_ctx ) ;
	if( nret )
		return -1;
	
	/* �������������� */
	AddOutputPluginEvent( p_env , p_logpipe_output_plugin , p_plugin_ctx->forward_sock );
	
	return 0;
}

funcOnOutputPluginEvent OnOutputPluginEvent;
int OnOutputPluginEvent( struct LogpipeEnv *p_env , struct LogpipeOutputPlugin *p_logpipe_output_plugin , void *p_context )
{
	struct OutputPluginContext	*p_plugin_ctx = (struct OutputPluginContext *)p_context ;
	
	int				nret = 0 ;
	
	/* �ر����� */
	DeleteOutputPluginEvent( p_env , p_logpipe_output_plugin , p_plugin_ctx->forward_sock );
	ERRORLOG( "remote socket closed , close forward sock[%d]" , p_plugin_ctx->forward_sock )
	close( p_plugin_ctx->forward_sock ); p_plugin_ctx->forward_sock = -1 ;
	
	/* ���ӷ���� */
	while( p_plugin_ctx->forward_sock == -1 )
	{
		sleep(2);
		nret = ConnectForwardSocket( p_plugin_ctx ) ;
		if( nret < 0 )
			return nret;
	}
	
	/* �������������� */
	AddOutputPluginEvent( p_env , p_logpipe_output_plugin , p_plugin_ctx->forward_sock );
	
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
	
	while( p_plugin_ctx->forward_sock == -1 )
	{
		sleep(2);
		nret = ConnectForwardSocket( p_plugin_ctx ) ;
		if( nret < 0 )
			return nret;
	}
	
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
	len = writen( p_plugin_ctx->forward_sock , comm_buf , 1+sizeof(uint16_t)+filename_len ) ;
	if( len == -1 )
	{
		ERRORLOG( "send comm magic and filename failed , errno[%d]" , errno )
		close( p_plugin_ctx->forward_sock ); p_plugin_ctx->forward_sock = -1 ;
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
	len = writen( p_plugin_ctx->forward_sock , & block_len_htonl , sizeof(block_len_htonl) ) ;
	if( len == -1 )
	{
		ERRORLOG( "send block len to socket failed , errno[%d]" , errno )
		return 1;
	}
	else
	{
		INFOLOG( "send block len to socket ok , [%d]bytes" , sizeof(block_len_htonl) )
		DEBUGHEXLOG( (char*) & block_len_htonl , len , NULL )
	}
	
	len = writen( p_plugin_ctx->forward_sock , block_buf , block_len ) ;
	if( len == -1 )
	{
		ERRORLOG( "send block data to socket failed , errno[%d]" , errno )
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
	len = writen( p_plugin_ctx->forward_sock , & block_len_htonl , sizeof(block_len_htonl) ) ;
	if( len == -1 )
	{
		ERRORLOG( "send block len to socket failed , errno[%d]" , errno )
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
	
	if( p_plugin_ctx->forward_sock >= 0 )
	{
		INFOLOG( "close forward sock[%d]" , p_plugin_ctx->forward_sock )
		close( p_plugin_ctx->forward_sock ); p_plugin_ctx->forward_sock = -1 ;
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

