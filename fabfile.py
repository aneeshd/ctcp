from fabric.api import *
from datetime import datetime
import threading


code_dir = '~/repos/demo_ctcp'

info = { 'ctcp'       : '18.62.16.187',
         'plato'      : '173.166.96.226',
         'localhost'  : '127.0.0.1'
         }

def host(user):
    print "running setup with user " + user
    if not info.has_key(user):
        raise Exception('The user ' + str(user) + ' does not exist')
    else:
        env.user = user
        env.hosts = [info[user]]

class ThreadRunServer(threading.Thread):
    '''Run a server instance locally'''
    def __init__(self, remake=False):
        self.remake_ = remake
        threading.Thread.__init__(self)


    def run(self, remake=False):
        if self.remake_:
            local('make remake')
        local('./srvctcp config/vegas')

def start_client(localIP, remake=False):
    with cd(code_dir):
        if remake:
            run('make remake')
        run('./clictcp -f shrimp.avi -h ' + str(localIP))

def test(localIP, user='ctcp', remake=False):
    t = ThreadRunServer(remake)
    t.start()
    start_client(localIP, remake)
    t.join()






