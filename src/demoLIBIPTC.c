/* demoLIBIPTC.c --- 
 * 
 * Filename: demoLIBIPTC.c
 * Description: 
 * Author: minji
 * Maintainer: 
 * Created: Wed Jul 13 18:27:36 2011 (-0400)
 * Version: 
 * Last-Updated: Thu Jul 14 13:36:21 2011 (-0400)
 *           By: minji
 *     Update #: 32
 * URL: 
 * Keywords: 
 * Compatibility: 
 * 
 */

/* Commentary: 
 * 
 * 
 * 
 */

/* Change log:
 * 
 * 
 */

/* This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth
 * Floor, Boston, MA 02110-1301, USA.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/errno.h>

#include <getopt.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <libiptc/libiptc.h>
//#include "iptables.h"
/* Code: */

int
main(void){

  struct iptc_handle* h = malloc(sizeof(struct iptc_handle));
  const char *chain = NULL;
  const char *tablename = NULL;
  
  h = iptc_init(tablename);
  if (!h)
    printf("blah!");
  return 0;

}



/* demoLIBIPTC.c ends here */
