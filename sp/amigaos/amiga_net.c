/*
 * Network stubs for the AmigaOS 3.x single player build.
 *
 * Single player only talks to its own server over the loopback channel,
 * which net_chan.c handles without the operating system, so no TCP/IP
 * stack is needed. Every real network call reports "no network".
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

netadr_t net_local_adr;

char *NET_BaseAdrToString( netadr_t a ) {
	static char s[64];

	Com_sprintf( s, sizeof( s ), "%i.%i.%i.%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3] );

	return s;
}

/* Only dotted quads: host name lookup needs a TCP/IP stack. */
qboolean Sys_StringToAdr( const char *s, netadr_t *a ) {
	int ip[4];
	int i;

	if ( sscanf( s, "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3] ) != 4 ) {
		return qfalse;
	}

	memset( a, 0, sizeof( *a ) );
	for ( i = 0 ; i < 4 ; i++ ) {
		if ( ip[i] < 0 || ip[i] > 255 ) {
			return qfalse;
		}
		a->ip[i] = ip[i];
	}
	a->type = NA_IP;

	return qtrue;
}

qboolean Sys_GetPacket( netadr_t *net_from, msg_t *net_message ) {
	return qfalse;
}

void Sys_SendPacket( int length, const void *data, netadr_t to ) {
}

qboolean Sys_IsLANAddress( netadr_t adr ) {
	return qtrue;
}

void Sys_ShowIP( void ) {
	Com_Printf( "No network support in this build.\n" );
}

void NET_Init( void ) {
	Com_Printf( "No network support in this build, single player only.\n" );
}

void NET_Shutdown( void ) {
}

void NET_Restart( void ) {
}

void NET_Sleep( int msec ) {
}
