import os, sys

# Include the scripts directory
path = os.path.abspath(os.path.join(os.path.dirname(__file__), 'scripts'))
if not path in sys.path:
    sys.path.insert(1,path)

import graph
from fabric.api import *
from datetime import datetime
import threading, Queue

code_dir = '~/repos/demo_ctcp'

info = { 'ctcp'       : '18.62.16.187',
         'plato'      : '173.166.96.226',
         'leo'        : '127.0.0.1'
         }

q = Queue.Queue()

def host(user):
    print "Running setup with user " + user
    if not info.has_key(user):
        raise Exception('The user ' + str(user) + ' does not exist')
    else:
        env.user = user
        env.hosts = [info[user]]

class ThreadRunServer(threading.Thread):
    '''Run a server instance locally'''
    def __init__(self, config, port, remake):
        self.remake_ = remake
        self.config_ = config
        self.port_   = port
        threading.Thread.__init__(self)

    def run(self, remake=False):
        if self.remake_:
            local('make remake')
        now = datetime.now()

        log_name =  '''logs/%(year)d-%(month)02d-%(day)02d/%(hour)02d:%(minute)02d.%(second)02d.log''' % \
        {'config' : self.config_,
         'year'   : now.year    ,
         'month'  : now.month   ,
         'day'    : now.day     ,
         'hour'   : now.hour    ,
         'minute' : now.minute  ,
         'second' : now.second }

        execute = '''./srvctcp -c config/%(config)s -l %(log)s -p %(port)s''' % \
        {'config': self.config_,
         'log'   : log_name,
         'port'  : self.port_}

        local(execute)
        q.put(log_name)

def start_client(localIP, file, port, remake):
    '''Run a client instance remotely with the user and host specified by env.user and env.hosts'''


    with cd(code_dir):
        if remake:
            run('make remake')
        execute = './clictcp -f %(file)s -h %(IP)s -p %(port)s' % \
        {'file' : file,
         'IP'   : localIP,
         'port' : port }

        run(execute)

def test(localIP, file='shrimp.avi', config='vegas', port='9999', remake=False):
    t = ThreadRunServer(config, port, remake)
    t.start()
    start_client(localIP, file, port, remake)
    t.join()
    log_name= q.get()
    path = os.path.abspath(os.path.join(os.path.dirname(__file__), log_name))
    graph.graph(path)
