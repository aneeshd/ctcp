/*****************************************************************************
*  MOCKS, a RFC1928 compliant SOCKSv5 server                         
*  Copyright (C) 2004  Dan Horobeanu <dhoro@spymac.com>
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
******************************************************************************/
#ifndef CHILD_REMOTE_H
#define CHILD_REMOTE_H

#define NEGBUFLEN    256
#define MAXIPSTRLEN  256

extern int             sk_client;
extern struct sockaddr ad_client;
extern int             sk_target;
extern struct sockaddr ad_target;

//---- config parameters - will be read from conf file by proxy_remote and passed to child_remote
typedef struct child_remote_cfg {
    int ctcp_probe;
    int debug;
    char cong_control[32];
    char logdir[256];
    int block_size;
    int num_blocks;
    int max_coding_wnd;
} child_remote_cfg;

int  handle_con(int ctcp_port, struct child_remote_cfg* cfg);
int  negotiate( struct sockaddr *ad_cl_local, int ctcp_port);
int  connect_client();
int  bind_client();
int  handle_traffic(int ctcp_port);

#endif /* CHILD_REMOTE_H */
