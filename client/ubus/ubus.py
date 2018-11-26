#!/usr/bin/env python3

import json
import urllib.request
import ssl
from functools import wraps

class UError(Exception):
    messages = [
        'Ok',
        'Invalid command',
        'Invalid argument',
        'Method not found',
        'Not found',
        'No data',
        'Permission denied',
        'Timeout',
        'Not supported',
        'Unknown error',
        'Connection failed',
    ]

    def __init__(self, val=None):
        self._val = val

    def __str__(self):
        if self._val != None and self._val < len(self.messages):
            return self.messages[self._val]
        return str(self._val)

class UObject(object):
    def __init__(self, ubus, basedir, uobject=None):
        self._ubus = ubus
        self._basedir = basedir
        self._object = uobject

    @property
    def _path(self):
        path = self._basedir
        if self._object != None:
            path += '.' + self._object
        return path

    def __call__(self, **params):
        return self._ubus.call(self._basedir, self._object, **params)

    def __getattr__(self, attr):
        return UObject(self._ubus, self._path, attr)

    def list(self, obj=None):
        path = self._path
        if obj != None:
            path += '.' + obj
        return self._ubus.list(path)

    def call(self, method, **params):
        return self._ubus.call(self._path, method, **params)

class UBus(object):
    def __init__(self, url, username=None, password=None,
                 timeout=300, ssl_context=None):
        self._url = url
        self._request_id = 0
        self._ubus_rpc_session = None
        self._ssl_context = ssl_context
        if self._ssl_context == None:
            self._ssl_context = ssl.create_default_context()
            self._ssl_context.check_hostname = False
            self._ssl_context.verify_mode = ssl.CERT_NONE
        if username != None:
            self.login(username, password, timeout)

    def jsonrpc_request(self, method, params, retry=False):
        self._request_id += 1
        req = {
            "jsonrpc": "2.0",
            "id": self._request_id,
            "method": method,
            "params": params,
        }
        headers = {'Content-Type': 'application/json'}
        #print("Sending: %s" % json.dumps(req))
        conn = urllib.request.urlopen(
            urllib.request.Request(self._url, json.dumps(req).encode('utf8'), headers),
            context=self._ssl_context)
        resp = json.loads(conn.read().decode('utf8'))
        #print("Response: %s" % resp)
        if resp.get('jsonrpc') != req['jsonrpc']:
            raise TypeError
        if resp.get('id') != req['id']:
            raise ValueError
        if resp.get('error') != None:
            raise ValueError(resp['error'])
        return resp.get('result')

    def _call(self, sessionid, obj, method, params):
        result = self.jsonrpc_request('call', [ sessionid, obj, method, params ])
        if result[0] != 0:
            raise UError(result[0])
        return result[1]

    def login(self, username, password, timeout=300):
        params = {
            'username': username,
            'password': password,
            'timeout': timeout,
        }
        result = self._call("00000000000000000000000000000000",
                            "session", "login", params)
        self._ubus_rpc_session = result['ubus_rpc_session']
        return result

    def call(self, obj, method, **params):
        return self._call(self._ubus_rpc_session, obj, method, params)

    def list(self, path=None):
        if path == None:
            path = '*'
        result = self.jsonrpc_request(
            'list', [ self._ubus_rpc_session, path, {}])
        return result

    def get_object(self, name):
        path = name.split('.')
        if len(path) > 1:
            obj = path[-1]
            path = '.'.join(path[:-1])
        else:
            path = path[0]
            obj = None
        return UObject(self, path, obj)

    def __getattr__(self, name):
        return self.get_object(name)

def method(func):
    types = func.__annotations__
    @wraps(func)
    def wrapper(self, **kwargs):
        for arg in kwargs:
            if arg not in types:
                raise TypeError('Argument %s is unknown' % arg)
            kwargs[arg] = types[arg](kwargs[arg])
        ret = func(self, **kwargs)
        return ret if ret is not None \
            else self.__getattr__(func.__name__)(**kwargs)
    return wrapper

if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(
        description='Client for ubus over http')
    parser.add_argument('url', metavar = 'URL')
    parser.add_argument('--username', required = False,
                        metavar = 'USERNAME')
    parser.add_argument('--password', required = False,
                        metavar = 'PASSWORD')
    parser.add_argument('--timeout', type = int, default = 10)
    parser.add_argument('object', metavar = 'OBJECT')
    parser.add_argument('method', metavar = 'METHOD')
    parser.add_argument('args', metavar = 'ARG=VAL', nargs = '*')

    args = parser.parse_args()
    ubus = UBus(args.url, args.username, args.password, args.timeout)
    margs = {}
    for arg in args.args:
        kv = arg.split('=', 1)
        if len(kv) < 2:
            raise ValueError('Arguments must have a name and value')
        try:
            margs[kv[0]] = int(kv[1])
        except:
            margs[kv[0]] = kv[1]
    print(ubus.call(args.object, args.method, **margs))
