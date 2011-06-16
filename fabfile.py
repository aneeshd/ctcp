from fabric.api import *
from datetime import datetime
import threading


code_dir = '~/repos/demo_ctcp'

info = { 'ctcp'       : '18.62.16.187',
         'plato'      : '173.166.96.226',
         'localhost'  : '127.0.0.1'
         }


def host(user):
    print "Running setup with user " + user
    if not info.has_key(user):
        raise Exception('The user ' + str(user) + ' does not exist')
    else:
        env.user = user
        env.hosts = [info[user]]

class ThreadRunServer(threading.Thread):
    '''Run a server instance locally'''
    def __init__(self, config, remake):
        self.remake_ = remake
        self.config_ = config
        threading.Thread.__init__(self)


    def run(self, remake=False):
        if self.remake_:
            local('make remake')
        now = datetime.now()
        local('./srvctcp'   +
              ' -c config/' + self.config_ +
              ' -l logs/%(year)d-%(month)02d-%(day)02d %(hour)02d:%(minute)02d.%(second)02d.log' %
              {'year'   : now.year,
               'month'  : now.month,
               'day'    : now.day,
               'hour'   : now.hour,
               'minute' : now.minute,
               'second' : now.second })

def start_client(localIP, file, remake):
    '''Run a client instance remotely with the user and host specified by env.user and env.hosts'''
    with cd(code_dir):
        if remake:
            run('make remake')
        run('./clictcp -f ' + file + ' -h ' + str(localIP))

def test(localIP, user='ctcp', file='shrimp.avi', config='vegas', remake=False):
    t = ThreadRunServer(config, remake)
    t.start()
    start_client(localIP, file, remake)
    t.join()






