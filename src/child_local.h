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
#ifndef CHILD_LOCAL_H
#define CHILD_LOCAL_H

#define NEGBUFLEN    256
#define MAXIPSTRLEN  256

extern int             sk_client;
extern struct sockaddr ad_client;
extern int             sk_target;
extern struct sockaddr ad_target;

//---- config parameters - will be read from conf file by proxy_local and passed to child_local
typedef struct child_local_cfg {
    char logdir[256];
    int debug;
    int ctcp_probe;
    int block_size;
    int num_blocks;
    int max_coding_wnd;
} child_local_cfg;

int  handle_con(struct child_local_cfg* cfg);
int  negotiate( struct sockaddr *ad_cl_local, struct child_local_cfg* cfg );
int  connect_client(struct child_local_cfg* cfg);
int  bind_client();
int  handle_traffic();
void *handle_ctcp_traffic();


#endif /* CHILD_LOCAL_H */
