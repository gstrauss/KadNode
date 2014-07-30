
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <net/if.h>
#include <unistd.h> /* close */
#include <sys/ioctl.h>

#include "main.h"
#include "conf.h"
#include "log.h"
#include "utils.h"
#include "net.h"
#include "kad.h"
#include "ext-lpd.h"


/* Multicast message format - inspired by, but not compatible to the BitTorrent Local Peer Discovery (LPD) */
const char msg_fmt[] =
	"DHT-SEARCH * HTTP/1.0\r\n"
	"Port: %u\r\n"
	"Server: KadNode\r\n"
	"Version: "MAIN_VERSION"\r\n"
	"\r\n"
	"\r\n";

enum { PACKET_LIMIT_MAX =  20 }; /* Packets per minute to be handled */
static int g_packet_limit = 0;
static IP g_lpd_addr = {0};
static int g_mcast_registered = 0; /* Indicates if the multicast addresses has been registered */
static time_t g_mcast_time = 0; /* Next time to perform a multicast ping */
static int g_sock_recv = -1;
static int g_sock_send = -1;


int mcast_join_group( int sock, IP *addr, const char ifce[] ) {
#if defined(MCAST_JOIN_GROUP) && !defined(__APPLE__)
	struct group_req req;

	if( ifce ) {
		if( (req.gr_interface = htonl( if_nametoindex( ifce ) )) == 0 ) {
			log_err( "LPD: Cannot find interface '%s' for multicast: %s", ifce, strerror( errno ) );
			return -1;
		}
	} else {
		req.gr_interface = 0;
	}

	memcpy( &req.gr_group, addr, addr_len( addr ) );

	if( setsockopt( sock, IPPROTO_IP, MCAST_JOIN_GROUP, &req, sizeof(req) ) < 0 ) {
		log_warn( "LPD: Failed to join multicast group: %s", strerror( errno ) );
		return -1;
	}

	return 0;
#else
	switch( addr->ss_family ) {
		case AF_INET: {
			struct ip_mreq mreq;
			struct ifreq ifreq;

			memcpy( &mreq.imr_multiaddr, &((IP4*) addr)->sin_addr, 4 );

			if( ifce ) {
				strncpy( ifreq.ifr_name, ifce, IFNAMSIZ );

				if( ioctl( sock, SIOCGIFADDR, &ifreq ) < 0 ) {
					log_err( "LPD: Cannot find interface '%s' for multicast: %s", ifce, strerror( errno ) );
					return -1;
				}
				memcpy( &mreq.imr_interface, &((struct sockaddr_in *) &ifreq.ifr_addr)->sin_addr, 4);
			} else {
				mreq.imr_interface.s_addr = htonl( INADDR_ANY );
			}

			if( setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0 ) {
				log_warn( "LPD: Failed to join IPv4 multicast group: %s", strerror( errno ) );
				return -1;
			}
			return 0;
		}
		case AF_INET6: {
			struct ipv6_mreq	mreq6;

			memcpy( &mreq6.ipv6mr_multiaddr, &((IP6*) addr)->sin6_addr, 16 );

			if( ifce ) {
				if( (mreq6.ipv6mr_interface = if_nametoindex( ifce )) == 0 ) {
					log_err( "LPD: Cannot find interface '%s' for multicast: %s", ifce, strerror( errno ) );
					return -1;
				}
			} else {
				mreq6.ipv6mr_interface = 0;
			}

			if( setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6, sizeof(mreq6)) < 0 ) {
				log_warn( "LPD: Failed to join IPv6 multicast group: %s", strerror( errno ) );
				return -1;
			}
			return 0;
		}
		default:
			return -1;
	}
#endif
}

int mcast_leave_group( int sock, IP *addr ) {
#if defined(MCAST_JOIN_GROUP) && !defined(__APPLE__)
	struct group_req req;

	req.gr_interface = 0;
	memcpy( &req.gr_group, addr, addr_len( addr ) );

	if( setsockopt( sock, IPPROTO_IP, MCAST_LEAVE_GROUP, &req, sizeof(req) ) < 0 ) {
		log_warn( "LPD: Failed to leave multicast group: %s", strerror( errno ) );
		return -1;
	}

	return 0;
#else
	switch( addr->ss_family ) {
		case AF_INET: {
			struct ip_mreq mreq;

			memcpy( &mreq.imr_multiaddr, &((IP4*) addr)->sin_addr, 4 );
			mreq.imr_interface.s_addr = htonl( INADDR_ANY );
			if( setsockopt( sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq) ) < 0 ) {
				log_warn( "LPD: Failed to leave IPv4 multicast group: %s", strerror( errno ) );
				return -1;
			}
			return 0;
		}
		case AF_INET6: {
			struct ipv6_mreq	mreq6;

			memcpy( &mreq6.ipv6mr_multiaddr, &((IP6 *) addr)->sin6_addr, 16 );
			mreq6.ipv6mr_interface = 0;
			if( setsockopt( sock, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq6, sizeof(mreq6) ) < 0 ) {
				log_warn( "LPD: Failed to leave IPv6 multicast group: %s", strerror( errno ) );
				return -1;
			}
			return 0;
		}
		default:
			return -1;
	}
#endif
}

const char *parse_packet_param( const char* str, const char* param ) {
	const char* pos;

	pos = strstr( str, param );
	if( pos == NULL ) {
		return NULL;
	} else {
		return (pos + strlen( param ));
	}
}

int parse_packet( const char *str ) {
	const char *beg;
	int port = 0;

	/* Parse port */
	beg = parse_packet_param( str, "Port: ");
	if( beg == NULL ) {
		return 0;
	}

	if( sscanf( beg, "%d\r\n", &port ) != 1 && port > 0 && port < 65536 ) {
		return 0;
	}

	/* Check for existence of server field */
	beg = parse_packet_param( str, "Server: ");
	if( beg == NULL ) {
		return 0;
	}

	/* Check for existence of version field */
	beg = parse_packet_param( str, "Version: ");
	if( beg == NULL ) {
		return 0;
	}

	return port;
}

int set_port( IP *addr, unsigned short port ) {
	if( addr->ss_family == AF_INET ) {
		((IP4 *)addr)->sin_port = htons( port );
	} else if( addr->ss_family == AF_INET6 ) {
		((IP6 *)addr)->sin6_port = htons( port );
	} else {
		return 1;
	}
	return 0;
}

void bootstrap_handle_mcast( int rc, int sock_recv ) {
	char addrbuf[FULL_ADDSTRLEN+1];
	char buf[512];
	IP c_addr;
	socklen_t addrlen;
	int rc_send;
	int rc_recv;

	if( g_mcast_time <= time_now_sec() ) {
		if( kad_count_nodes( 0 ) == 0 ) {
			/* Join multicast group if possible */
			if( g_mcast_registered == 0 && mcast_join_group( g_sock_recv, &g_lpd_addr, gconf->dht_ifce ) == 0 ) {
				log_info( "LPD: No peers known. Joined multicast group." );
				g_mcast_registered = 1;
			}

			if( g_mcast_registered == 1 ) {
				snprintf( buf, sizeof(buf), msg_fmt, atoi( gconf->dht_port ) );

				addrlen = addr_len( &g_lpd_addr );
				rc_send = sendto( g_sock_send, buf, strlen( buf ), 0, (struct sockaddr*) &g_lpd_addr, addrlen );
				if( rc_send < 0 ) {
					log_warn( "LPD: Cannot send multicast message: %s", strerror( errno ) );
				} else {
					log_info( "LPD: Send multicast message to find nodes." );
				}
			}
		}

		/* Cap number of received packets to 10 per minute */
		g_packet_limit = 5 * PACKET_LIMIT_MAX;

		/* Try again in ~5 minutes */
		g_mcast_time = time_add_min( 5 );
	}

	if( rc <= 0 ) {
		return;
	}

	/* Reveice multicast ping */
	addrlen = sizeof(IP);
	rc_recv = recvfrom( g_sock_recv, buf, sizeof(buf), 0, (struct sockaddr*) &c_addr, (socklen_t*) &addrlen );
	if( rc_recv < 0 ) {
		log_warn( "LPD: Cannot receive multicast message: %s", strerror( errno ) );
		return;
	}

	if( g_packet_limit < 0 ) {
		/* Too much traffic - leave multicast group for now */
		if( g_mcast_registered == 1 && mcast_leave_group( g_sock_recv, &g_lpd_addr ) == 0 ) {
			log_warn( "LPD: Too much traffic. Left multicast group." );
			g_mcast_registered = 0;
		}
		return;
	} else {
		g_packet_limit--;
	}

	if( rc_recv >= sizeof(buf) ) {
		return;
	} else {
		buf[rc_recv] = '\0';
	}

	int port = parse_packet( buf );
	if( port > 0 ) {
		set_port( &c_addr, port );
		log_debug( "LPD: Ping lonely peer at %s", str_addr( &c_addr, addrbuf ) );
		kad_ping( &c_addr );
	} else {
		log_debug( "LPD: Received invalid packet on multicast group." );
	}
}

int multicast_disable_loop( int sock, int af ) {
	const int opt_off = 1;
	int optname;

	/* We don't want to receive our own packets */
	optname = (af == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6;
	if( setsockopt( sock, optname, IP_MULTICAST_LOOP, &opt_off, sizeof(opt_off) ) < 0 ) {
		log_warn( "LPD: Failed to set IP_MULTICAST_LOOP: %s", strerror( errno ) );
		return -1;
	}

	return 0;
}

int create_send_socket( void ) {
	const int scope = 1;
	int sock_send;

	sock_send = net_socket( "LPD", gconf->dht_ifce, IPPROTO_UDP, gconf->af );

	if( setsockopt( sock_send, IPPROTO_IP, IP_MULTICAST_TTL, &scope, sizeof(scope) ) < 0 ) {
		log_err( "LPD: Failed to set IP_MULTICAST_TTL for sending socket: %s", strerror( errno ));
		goto fail;
	}

	if( multicast_disable_loop( sock_send, gconf->af ) < 0 ) {
		goto fail;
	}

	return sock_send;

fail:
	close( sock_send );
	return -1;
}

int create_receive_socket( void ) {
	int sock_recv;

	sock_recv = net_bind( "LPD", gconf->lpd_addr, DHT_PORT_MCAST, gconf->dht_ifce, IPPROTO_UDP, gconf->af );

	/* We don't want to receive our own packets */
	if( multicast_disable_loop( sock_recv, gconf->af ) < 0 ) {
		close( sock_recv );
		return -1;
	}

	return sock_recv;
}

void lpd_setup( void ) {

	g_packet_limit = PACKET_LIMIT_MAX;
	if( addr_parse( &g_lpd_addr, gconf->lpd_addr, DHT_PORT_MCAST, gconf->af ) != 0 ) {
		log_err( "BOOT: Failed to parse IP address for '%s'.", gconf->lpd_addr );
	}

	if( gconf->lpd_disable ) {
		return;
	}

	/*
	* Use different sockets for sending and receiving because
	* MacOSX does not seem to allow it to be the same.
	*/
	g_sock_send = create_send_socket();
	g_sock_recv = create_receive_socket();

	net_add_handler( g_sock_recv, &bootstrap_handle_mcast );
}

void lpd_free( void ) {
	close( g_sock_send );
	close( g_sock_recv );
}