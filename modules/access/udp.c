/*****************************************************************************
 * udp.c: raw UDP input module
 *****************************************************************************
 * Copyright (C) 2001-2005 VLC authors and VideoLAN
 * Copyright (C) 2007 Remi Denis-Courmont
 * $Id: 8139d05a0079b5e3d9506eb5010aedfa8c6b8d00 $
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *          Tristan Leteurtre <tooney@via.ecp.fr>
 *          Laurent Aimar <fenrir@via.ecp.fr>
 *          Jean-Paul Saman <jpsaman #_at_# m2x dot nl>
 *          Remi Denis-Courmont
 *
 * Reviewed: 23 October 2003, Jean-Paul Saman <jpsaman _at_ videolan _dot_ org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_access.h>
#include <vlc_network.h>
#include <vlc_block.h>

#define MTU 65535

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int  Open( vlc_object_t * );
static void Close( vlc_object_t * );
static void *Threadsendkeep_alive(void *data);

#define BUFFER_TEXT N_("Receive buffer")
#define BUFFER_LONGTEXT N_("UDP receive buffer size (bytes)" )

vlc_module_begin ()
    set_shortname( N_("UDP" ) )
    set_description( N_("UDP input") )
    set_category( CAT_INPUT )
    set_subcategory( SUBCAT_INPUT_ACCESS )

    add_obsolete_integer( "server-port" ) /* since 2.0.0 */
    add_integer( "udp-buffer", 0x400000, BUFFER_TEXT, BUFFER_LONGTEXT, true )

    set_capability( "access", 0 )
    add_shortcut( "udp", "udpstream", "udp4", "udp6" )

    set_callbacks( Open, Close )
vlc_module_end ()

struct access_sys_t
{
    int fd;
    size_t fifo_size;
    block_fifo_t *fifo;
    vlc_thread_t thread;
	vlc_thread_t kplv_thread;

     SOCKADDR from;
     int rec_flag;

	int kplv_flag;
};

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static block_t *BlockUDP( access_t * );
static int Control( access_t *, int, va_list );
static void* ThreadRead( void *data );

/*****************************************************************************
 * Open: open the socket
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    access_t     *p_access = (access_t*)p_this;
    access_sys_t *sys = malloc( sizeof( *sys ) );
    if( unlikely( sys == NULL ) )
        return VLC_ENOMEM;

    p_access->p_sys = sys;

    /* Set up p_access */
    access_InitFields( p_access );
    ACCESS_SET_CALLBACKS( NULL, BlockUDP, Control, NULL );

    char *psz_name = strdup( p_access->psz_location );
    char *psz_parser;

    // vvv wenfeng
    char *psz_parser_nat;
	char *psz_parser_kplv;
	char nat_ip[20];
	int nat_port;
    // ^^^ wenfeng

    const char *psz_server_addr, *psz_bind_addr = "";
    int  i_bind_port = 1234, i_server_port = 0;

    if( unlikely(psz_name == NULL) )
        goto error;

    /* Parse psz_name syntax :
     * [serveraddr[:serverport]][@[bindaddr]:[bindport]] */
    psz_parser = strchr( psz_name, '@' );
    if( psz_parser != NULL )
    {
        /* Found bind address and/or bind port */
        *psz_parser++ = '\0';
        psz_bind_addr = psz_parser;

        if( psz_bind_addr[0] == '[' )
            /* skips bracket'd IPv6 address */
            psz_parser = strchr( psz_parser, ']' );

        if( psz_parser != NULL )
        {
            psz_parser = strchr( psz_parser, ':' );
            if( psz_parser != NULL )
            {
                *psz_parser++ = '\0';
                i_bind_port = atoi( psz_parser );
            }
        }
    }

    psz_server_addr = psz_name;
    psz_parser = ( psz_server_addr[0] == '[' )
        ? strchr( psz_name, ']' ) /* skips bracket'd IPv6 address */
        : psz_name;

    if( psz_parser != NULL )
    {
        psz_parser = strchr( psz_parser, ':' );
        if( psz_parser != NULL )
        {
            *psz_parser++ = '\0';
            i_server_port = atoi( psz_parser );
        }
    }

    msg_Dbg( p_access, "opening server=%s:%d local=%s:%d",
             psz_server_addr, i_server_port, psz_bind_addr, i_bind_port );

    sys->fd = net_OpenDgram( p_access, psz_bind_addr, i_bind_port,
                             psz_server_addr, i_server_port, IPPROTO_UDP );
  
	free( psz_name );
    if( sys->fd == -1 )
    {
        msg_Err( p_access, "cannot open socket" );
        goto error;
    }

    sys->fifo = block_FifoNew();
    if( unlikely( sys->fifo == NULL ) )
    {
        net_Close( sys->fd );
        goto error;
    }
    // vvv wenfeng
	psz_parser_nat = strstr(p_access->psz_location, "nat=");
	msg_Dbg(p_access , "psz_name = %s psz_parser_nat = %s\n", p_access->psz_location, psz_parser_nat);

	memset(nat_ip,0,20);
	if(psz_parser_nat != NULL)
	{
		psz_parser_nat +=4;

		int i=0;	
		for(i=0; i< 20; i++)
		{
			nat_ip[i] = *(psz_parser_nat++);
			//msg_Dbg("nat_ip=[%d]%d \n", i, nat_ip[i]);
			if(*psz_parser_nat == ':')
			{	
				break;
			}
		}
		psz_parser_nat++;
		msg_Dbg(p_access , "psz_parser_nat--- = %s \n", psz_parser_nat);

		nat_port = atoi(psz_parser_nat);

		msg_Dbg(p_access , "nat_ip = %s nat_port = %d\n", nat_ip, nat_port);

		struct sockaddr_in RecvAddr;
	 	int iResult;
	 	RecvAddr.sin_family = AF_INET;
		RecvAddr.sin_port = htons(nat_port);
		RecvAddr.sin_addr.s_addr = inet_addr(nat_ip);

	  
	   iResult = sendto(sys->fd,
		                 "helloword", 10, 0, (SOCKADDR *) &RecvAddr, sizeof (RecvAddr));
		if (iResult == -1) {
		    msg_Dbg( p_access, "sendto failed with error: %d\n", WSAGetLastError());
		 
		}
		 
	} 

 
    // ^^^ wenfeng

    sys->fifo_size = var_InheritInteger( p_access, "udp-buffer");

    if( vlc_clone( &sys->thread, ThreadRead, p_access,
                   VLC_THREAD_PRIORITY_INPUT ) )
    {
        block_FifoRelease( sys->fifo );
        net_Close( sys->fd );
error:
        free( sys );
        return VLC_EGENERIC;
    }
	
	// vvv wenfeng
	psz_parser_kplv = strstr( p_access->psz_location, "kplv=");
	if(psz_parser_kplv !=  NULL)
	{
 	    vlc_clone( &sys->kplv_thread, Threadsendkeep_alive, p_access,
                   VLC_THREAD_PRIORITY_LOW ) ;
	    sys->kplv_flag = 1;
	}
	else
	{
		 sys->kplv_flag = 0;
	}
    // ^^^ wenfeng
	 

    return VLC_SUCCESS;
}


// vvv wenfeng
static void *Threadsendkeep_alive(void *data)
{
    access_t *access = data;
    access_sys_t *sys = access->p_sys;
 	struct sockaddr_in RecvAddr;

  char *psz_name = strdup( access->psz_location );
  char *psz_parser;
	char kplv_ip[20];
	int kplv_port;	
	int time_interval = 3000000;
	char content[40]="helloword";
	int i=0;	
	int len =0;
	psz_parser = strstr(psz_name, "kplv=");
	memset(kplv_ip, 0 ,20);

	if(psz_parser != NULL)
	{
		psz_parser +=5;
	
		for(i=0; i< 20; i++)
		{
			kplv_ip[i] = *(psz_parser++);

			if(*psz_parser == ':')
			{	
				break;
			}
		}

	}
	psz_parser++;
	kplv_port = atoi(psz_parser);
	msg_Dbg(access , "keep_ip = %s keep_port = %d\n", kplv_ip, kplv_port);
 
	psz_parser = strstr(psz_name, "time=");
	if(psz_parser != NULL)
	{
		psz_parser +=5;
		time_interval = atoi(psz_parser) * 100000;
		msg_Dbg(access , " time_interval = %d\n",  time_interval);
	}

	
	psz_parser = strstr(psz_name, "string=");

	if(psz_parser != NULL)
	{
		memset(content, 0, 40);
		psz_parser +=7;
		for(i=0; i<30; i++)
		{
			content[i] = *psz_parser++;
			if(*psz_parser==';')
			break;
		}
	}
	 
	psz_parser = strstr(psz_name, "strlen=");
	if(psz_parser != NULL)
	{
		psz_parser +=7;
		len = atoi(psz_parser) ;
		msg_Dbg(access , " len = %d\n",  len);
	}
	else
	{
		len = strlen(content);
	}


 	RecvAddr.sin_family = AF_INET;
    RecvAddr.sin_port = htons(kplv_port);
    RecvAddr.sin_addr.s_addr = inet_addr(kplv_ip);

    int iResult;
  
    unsigned char buf[8]= {0x80,0xc9,0x00,0x01,0,0,0,1};
    unsigned int *p;
    unsigned int cnt=0;
   for(;;)
   	{
  	
		cnt++;
		p= (int*)&buf[4];
		*p = htonl(cnt);
		if(sys->rec_flag == 1)
	   {
    	    iResult = sendto(sys->fd,
		                 buf, 8, 0, (SOCKADDR *) &sys->from, sizeof (RecvAddr));
       }
		else
		{
		   iResult = sendto(sys->fd,
		                 buf, 8, 0, (SOCKADDR *) & RecvAddr, sizeof (RecvAddr));
		}

		if (iResult == -1) {
		    msg_Dbg( access, "sendto failed with error: %d\n", WSAGetLastError());

		}
 		msg_Dbg( access, "send len %d\n", iResult);
		msleep(time_interval);

	  
	  
		//  msg_Dbg( access, "hello word length = %d	\n", bytesSent);
		
		if( errno == EINTR )
		   break;

   	}
  
}
// ^^^ wenfeng
/*****************************************************************************
 * Close: free unused data structures
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    access_t     *p_access = (access_t*)p_this;
    access_sys_t *sys = p_access->p_sys;

	if(sys->kplv_flag == 1)
	{
    	vlc_cancel( sys->kplv_thread );
    	vlc_join( sys->kplv_thread, NULL );
	}

    vlc_cancel( sys->thread );
    vlc_join( sys->thread, NULL );
    block_FifoRelease( sys->fifo );
    net_Close( sys->fd );
    free( sys );
}

/*****************************************************************************
 * Control:
 *****************************************************************************/
static int Control( access_t *p_access, int i_query, va_list args )
{
    bool    *pb_bool;
    int64_t *pi_64;

    switch( i_query )
    {
        case ACCESS_CAN_SEEK:
        case ACCESS_CAN_FASTSEEK:
        case ACCESS_CAN_PAUSE:
        case ACCESS_CAN_CONTROL_PACE:
            pb_bool = (bool*)va_arg( args, bool* );
            *pb_bool = false;
            break;

        case ACCESS_GET_PTS_DELAY:
            pi_64 = (int64_t*)va_arg( args, int64_t * );
            *pi_64 = INT64_C(1000)
                   * var_InheritInteger(p_access, "network-caching");
            break;

        default:
            return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

/*****************************************************************************
 * BlockUDP:
 *****************************************************************************/
static block_t *BlockUDP( access_t *p_access )
{
    access_sys_t *sys = p_access->p_sys;
    block_t *block;

    if( p_access->info.b_eof )
        return NULL;

    block = block_FifoGet( sys->fifo );
    p_access->info.b_eof = block == NULL;
    return block;
}

/*****************************************************************************
 * ThreadRead: Pull packets from socket as soon as possible.
 *****************************************************************************/
static void* ThreadRead( void *data )
{
    access_t *access = data;
    access_sys_t *sys = access->p_sys;

   // vvv wenfeng
    char tmp[MTU];
    int n;
    int SenderAddrSize = sizeof (SOCKADDR);
   // ^^^ wenfeng
    FILE *fp;
    fp = fopen("c:\\test.ts","rb+");
    char tx_buf[20];
	char *p;
	memset(tx_buf,0,20);
 	int size = fread(tx_buf,1,20,fp);
	msg_Dbg( access,"tx_buf =%s ",tx_buf);
    p = strstr(tx_buf,"saveudp");
    if(p !=NULL)
	{
		msg_Dbg( access,"save udp video ");
	}
    for( ;; )
    {

 		if(sys->rec_flag != 1 && sys->kplv_flag == 1)
		{
			static int cnt=0;
			n = recvfrom(sys->fd, tmp, MTU, 0, (SOCKADDR*) &sys->from, &SenderAddrSize);
			if(n > 0)
			{
			   sys->rec_flag = 1;
			//   ioctlsocket (sys->fd, FIONBIO, &(unsigned long){ 1 });
				fwrite(tmp,n,1,fp);
			//	msg_Dbg( access,"Send msg back to IP:[%s] Port:[%d]\n", inet_ntoa(sys->from), ntohs(sys->from));  
				msg_Dbg( access,"recv remote address ");

			  block_t *pkt1;
				//	ssize_t len;

					block_FifoPace( sys->fifo, SIZE_MAX, sys->fifo_size );

					pkt1 = block_Alloc( MTU );
					if( unlikely( pkt1 == NULL ) )
						break;
			
					block_cleanup_push( pkt1 );
					memcpy(pkt1->p_buffer,tmp,n);
 					vlc_cleanup_pop();

					pkt1 = block_Realloc( pkt1, 0, n );
					block_FifoPut( sys->fifo, pkt1 );

			}

			else if (n == EWOULDBLOCK || n == EAGAIN)  
			{
				msg_Dbg( access,"recvfrom timeout");
			}
			else
			{
			  msg_Dbg( access, "|||||| error %d\n", WSAGetLastError());
			}

		}
        block_t *pkt;
        ssize_t len;

        block_FifoPace( sys->fifo, SIZE_MAX, sys->fifo_size );

        pkt = block_Alloc( MTU );
        if( unlikely( pkt == NULL ) )
            break;

        block_cleanup_push( pkt );
        len = net_Read( access, sys->fd, NULL, pkt->p_buffer, MTU, false );
		// vvv wenfeng
		if(p !=NULL && len>0 && size < 10)
		//if(len>0)
		{
			//msg_Dbg( access,"savedata");
			fwrite(pkt->p_buffer,len,1,fp);
		}
		// ^^^ wenfeng
        vlc_cleanup_pop();

        if( len == -1 )
        {
            block_Release( pkt );

            if( errno == EINTR )
                break;
            continue;
        }

        pkt = block_Realloc( pkt, 0, len );
        block_FifoPut( sys->fifo, pkt );
    }
	fflush(fp); 
	fclose(fp); // wenfeng
    block_FifoWake( sys->fifo );
    return NULL;
}
