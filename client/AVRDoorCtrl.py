#!/usr/bin/env python3

import serial
import struct
import crcmod
import ubus
import json
import time
import calendar
import logging
import functools
import base64
from urllib.parse import urldefrag

try:
    from Cryptodome.Hash import HMAC, SHA1
except ModuleNotFoundError:
    HMAC = None
    SHA1 = None

class AVRDoorCtrlUartTransport(object):
    """
    Implement send and receiving messages over a serial port.
    The messages are transmitted using the following format:

      STE CMD LENGTH PAYLOAD CRC

    where
      STE:	Start byte (\x7e)
      CMD:	Command type (uint8_t)
      LENGTH:	Length of payload (uint8_t)
      CRC:	Xmodem CRC of the CMD-LENGTH-PAYLOAD (uint16_t le)

    Furthermore all the data after the start byte is escaped using:
       0x7E <-> 0x7D 0x5E
       0x7D <-> 0x7D 0x5D
    """
    def __init__(self, port, timeout=5):
        self._tty = serial.Serial(port, 38400, timeout=timeout)

    @staticmethod
    def compute_crc(data):
        crc = crcmod.predefined.Crc('xmodem')
        crc.update(data)
        return crc.crcValue

    @classmethod
    def escape_msg(cls, msg):
        return msg.replace(b'\x7d', b'\x7d\x5d').replace(b'\x7e', b'\x7d\x5e')

    @classmethod
    def unescape_msg(cls, msg):
        return msg.replace(b'\x7d\x5e', b'\x7e').replace(b'\x7d\x5d', b'\x7d')

    def read_msg(self):
        sync = False
        escape = False
        type = None
        length = None
        data = b''
        pos = 0
        crc = None

        while True:
            c = self._tty.read(1)
            if len(c) < 1:
                raise TimeoutError
            c = c[0]
            if c == 0x7E:
                sync = True
                escape = False
                type = None
                length = None
                continue

            # Skip everything until we sync
            if sync != True:
                continue

            # Handle escaped bytes
            if escape == True:
                c ^= 0x20
                escape = False
            elif c == 0x7D:
                escape = True
                continue

            # Read the message type
            if type == None:
                type = c
                continue

            # Read the payload size
            if length == None:
                length = c
                continue

            # Read the payload
            if pos < length:
                data += struct.pack("B", c)
                pos += 1
                continue

            # Read the first CRC byte
            if crc == None:
                crc = c
                continue

            # Read the second CRC byte
            crc |= c << 8

            # Check the CRC
            data_crc = self.compute_crc(struct.pack("BB", type, length) + data)
            if crc != data_crc:
                raise ValueError

            return type, data

    def send_msg(self, type, payload = None):
        # Get and check the payload size
        if payload != None:
            length = len(payload)
            if length > 255:
                raise ValueError
        else:
            length = 0
            payload = b''
        # Assemble the message
        pkt = struct.pack("BB", type, length) + payload
        pkt += struct.pack("<H", self.compute_crc(pkt))
        # Format the packet
        pkt = b'\x7e' + self.escape_msg(pkt)
        # And send it out
        self._tty.write(pkt)

class AVRDoorCtrlError(Exception):
    EPERM = 1
    ENOENT = 2
    EINTR = 4
    EIO = 5
    E2BIG = 7
    ENOMEM = 12
    EFAULT = 14
    EBUSY = 16
    EEXIST = 17
    ENODEV = 19
    EINVAL = 22
    ENOSPC = 28
    ERANGE = 34

    _errors = {
        EPERM: "Operation not permitted",
        ENOENT: "No such file or directory",
        EINTR: "Interrupted system call",
        EIO: "I/O error",
        E2BIG: "Argument list too long",
        ENOMEM: "Out of memory",
        EFAULT: "Bad address",
        EBUSY: "Device or resource busy",
        EEXIST: "File exists",
        ENODEV: "No such device",
        EINVAL: "Invalid argument",
        ENOSPC: "No space left on device",
        ERANGE: "Out of range",
    }

    def __init__(self, errno):
        self.errno = errno

    @classmethod
    def strerror(cls, errno):
        return cls._errors.get(errno, f"Error {errno}")

    def __str__(self):
        return self.strerror(self.errno)

class AVRDoorCtrlSerialHandler(object):
    CMD_GET_DEVICE_DESCRIPTOR = 0
    CMD_GET_TIME = 2
    CMD_SET_TIME = 3
    CMD_GET_CONTROLLER_CONFIG = 4
    CMD_SET_CONTROLLER_CONFIG = 5
    CMD_GET_DOOR_CONFIG = 10
    CMD_SET_DOOR_CONFIG = 11
    CMD_GET_ACCESS_RECORD = 20
    CMD_SET_ACCESS_RECORD = 21
    CMD_SET_ACCESS = 22
    CMD_REMOVE_ALL_ACCESS = 23
    CMD_GET_ACCESS = 24
    CMD_GET_USED_ACCESS = 25
    CMD_GET_ACCESS_RECORD_V2 = 30
    CMD_SET_ACCESS_RECORD_V2 = 31
    CMD_SET_ACCESS_V2 = 32
    CMD_GET_ACCESS_V2 = 33
    CMD_GET_USED_ACCESS_V2 = 34

    EVENT_BASE = 127
    EVENT_STARTED = EVENT_BASE + 0
    EVENT_ACCESS = EVENT_BASE + 1
    EVENT_DOOR_STATUS = EVENT_BASE + 2

    REPLY_OK = 0
    REPLY_ERROR = 255

    access_record_types = ("none", "pin", "card", "card+pin")

    ACCESS_TYPE_NONE = 0
    ACCESS_TYPE_PIN = 1
    ACCESS_TYPE_CARD = 2
    ACCESS_TYPE_CARD_AND_PIN = ACCESS_TYPE_CARD | ACCESS_TYPE_PIN

    ACCESS_RECORD_TYPE_CARD_NONE = 0
    ACCESS_RECORD_TYPE_CARD_ID = 1

    ACCESS_RECORD_TYPE_PIN_NONE = 0
    ACCESS_RECORD_TYPE_PIN_FIXED = 1 << 1
    ACCESS_RECORD_TYPE_PIN_HOTP = 2 << 1
    ACCESS_RECORD_TYPE_PIN_TOTP = 3 << 1

    CONTROLLER_KEY_SIZE = 20

    @staticmethod
    def parse_version(version):
        major, minor = version.split('.')
        return int(major) * 1000 + int(minor)

    def __init__(self, dev, *args, controller_version = None, **kwargs):
        self._events = []
        self._descriptor = None
        if dev.startswith('/dev/tty'):
            self._transport = AVRDoorCtrlUartTransport(dev, *args, **kwargs)
        else:
            raise ValueError
        try:
            type, payload = self.read_msg()
            if type != self.EVENT_STARTED:
                raise TypeError("Initial event is not a start event")

            if len(payload) > 0:
                self._descriptor = self.unpack_device_descriptor(payload)
                version = self._descriptor['version']
            else:
                # Version 0.2 doesn't send the device descriptor at start
                version = '0.2'
        except TimeoutError:
            # Version 0.1 doesn't send a start event at all
            version = '0.1'
        except:
            raise

        logging.info(f"Device version {version} started")
        if controller_version is not None:
            logging.warning(f"Forcing controller version {controller_version}")
            version = controller_version
        self._version = self.parse_version(version)

    @staticmethod
    def since_version(version):
        def decorator(version, method):
            @functools.wraps(method)
            def wrapper(self, *args, **kwargs):
                if self._version < version:
                    raise NotImplementedError
                return method(self, *args, **kwargs)
            return wrapper
        return functools.partial(decorator, version)

    def read_msg(self):
        type, payload = self._transport.read_msg()
        logging.debug("Got %02x - %s" % (type, ":".join("{:02x}".format(c) for c in payload)))
        return type, payload

    def read_event(self):
        if len(self._events) > 0:
            type, payload = self._events.pop(0)
        else:
            type, payload = self.read_msg()
        return self.parse_event(type, payload)

    def send_msg(self, type, payload = None):
        desc = (" - " + ":".join("{:02x}".format(c) for c in payload)) \
            if payload is not None else ""
        logging.debug("Send: %02x%s" % (type, desc))
        self._transport.send_msg(type, payload)

    def send_cmd(self, type, payload = None, response_size=0):
        self.send_msg(type, payload)
        # Read the incoming messages, but queue the events
        while True:
            reply, response = self.read_msg()
            if self.is_event(reply):
                self._events.append((reply, response))
            else:
                break
        # Raise errors
        if reply == self.REPLY_ERROR:
            if len(response) < 1:
                raise Exception("Invalid error reply")
            errno, = struct.unpack("b", response)
            raise AVRDoorCtrlError(-errno)
        # Make sure we got an OK
        if reply != self.REPLY_OK:
            raise Exception("Bad reply type: %d" % reply)
        # Make sure we got enough data
        if len(response) < response_size:
            raise Exception("Bad response length: %d" % len(response))
        return response

    @classmethod
    def is_event(cls, type):
        return type >= cls.EVENT_BASE and type < cls.REPLY_ERROR

    @staticmethod
    def pack_pin(pin):
        val = 0xFFFFFFFF
        for c in pin:
            val = ((val << 4) & 0xFFFFFFFF) | int(c, 10)
        return val

    @staticmethod
    def unpack_pin(pin):
        s = ''
        for i in range(7, -1, -1):
            d = (pin >> (i * 4)) & 0xF
            if d != 0xF:
                s += '%d' % d
        return s

    @staticmethod
    def unpack_device_descriptor(response):
        major, minor, doors, records = struct.unpack("<BBBH", response[0:5])
        ret = {
            "version": "%d.%d" % (major, minor),
            "num_doors": doors,
            "num_access_records" : records,
        }
        if len(response) > 5:
            ret["free_access_records"], = struct.unpack("<H", response[5:7])
        return ret

    def get_device_descriptor(self):
        if self._descriptor:
            return self._descriptor
        response = self.send_cmd(
            self.CMD_GET_DEVICE_DESCRIPTOR, None, 5)
        return self.unpack_device_descriptor(response)

    def get_controller_config(self):
        response = self.send_cmd(
            self.CMD_GET_CONTROLLER_CONFIG, None,
            self.CONTROLLER_KEY_SIZE)
        key = response[0:20]
        ret = {
            "key": base64.b16encode(key).decode('ascii'),
        }
        return ret

    def set_controller_config(self, root_key=None):
        if root_key is not None:
            key = base64.b16decode(root_key).zfill(self.CONTROLLER_KEY_SIZE)
            key = key[:self.CONTROLLER_KEY_SIZE]
        else:
            key = b'\x00' * self.OTP_KEY_SIZE
        self.send_cmd(self.CMD_SET_CONTROLLER_CONFIG, key)
        return {}

    def get_door_config(self, index):
        response = self.send_cmd(self.CMD_GET_DOOR_CONFIG,
                                 struct.pack("<B", int(index)), 7)
        open_time, = struct.unpack("<H", response[0:2])
        ret = {
            "open_time": open_time,
        }
        return ret

    def set_door_config(self, index, open_time):
        req = struct.pack("<BHHHB", int(index), int(open_time), 0, 0, 0)
        self.send_cmd(self.CMD_SET_DOOR_CONFIG, req)
        return {}

    @classmethod
    def _unpack_access_record(self, response, pin = None, card = None):
        key, access = struct.unpack("<LB", response[0:5])
        # If the record is invalid ignore it
        if access & (1 << 2):
            access = 0
        type = access & 0x3
        doors = (access >> 4) & 0xF
        ret = {
            "used": bool(access & (1 << 3)),
        }
        if type != self.ACCESS_TYPE_NONE:
            ret['doors'] = doors
        if type == self.ACCESS_TYPE_PIN:
            ret['pin'] = self.unpack_pin(key)
        elif type == self.ACCESS_TYPE_CARD:
            ret['card'] = key
        elif type == self.ACCESS_TYPE_CARD_AND_PIN:
            if pin != None:
                ret['pin'] = pin
                ret['card'] = key ^ self.pack_pin(pin)
            elif card != None:
                ret['pin'] = self.unpack_pin(key ^ int(card))
                ret['card'] = card
            else:
                ret['card+pin'] = key
        return ret

    def get_access_record(self, index, pin = None, card = None):
        response = self.send_cmd(self.CMD_GET_ACCESS_RECORD,
                                 struct.pack("<H", int(index)), 5)
        ret = self._unpack_access_record(response, pin, card)
        ret['index'] = index
        return ret

    @classmethod
    def _unpack_access_record_v2(self, response):
        hdr, card, pin = struct.unpack("<BLL", response[0:9])
        ret = {
            "doors": (hdr >> 4) & 0xF,
            "used": bool(hdr & (1 << 3)),
        }

        card_type = hdr & 1
        if card_type == self.ACCESS_RECORD_TYPE_CARD_ID:
            ret['card_type'] = 'id'
            ret['card'] = card

        pin_type = hdr & (3 << 1)
        if pin_type == self.ACCESS_RECORD_TYPE_PIN_FIXED:
            ret['pin_type'] = 'fixed'
            ret['pin'] = self.unpack_pin(pin)
        elif pin_type in (self.ACCESS_RECORD_TYPE_PIN_HOTP,
                          self.ACCESS_RECORD_TYPE_PIN_TOTP):
            ret['pin_type'] = 'hotp' \
                if pin_type == self.ACCESS_RECORD_TYPE_PIN_HOTP \
                else 'totp'
            ret['otp_key'] = pin & 0x3FF
            ret['otp_digits'] = ((pin >> 10) & 3) + 6
            if pin_type == self.ACCESS_RECORD_TYPE_PIN_HOTP:
                ret['hotp_resync_limit'] = (pin >> 12) & 0xF
                ret['hotp_counter'] = (pin >> 16) & 0xFFFF
            else:
                ret['totp_allow_followings'] = (pin >> 12) & 3
                ret['totp_allow_previous'] = (pin >> 14) & 3
                ret['totp_interval'] = (pin >> 16) & 0xFFFF

        return ret

    @since_version(3)
    def get_access_record_v2(self, index):
        response = self.send_cmd(self.CMD_GET_ACCESS_RECORD_V2,
                                 struct.pack("<H", int(index)), 5)
        ret = self._unpack_access_record_v2(response)
        ret['index'] = index
        return ret

    @classmethod
    def _pack_access_record(self, pin = None, card = None,
                            doors = 0, used = False, card_pin = None):
        if card_pin != None:
            type = self.ACCESS_TYPE_CARD_AND_PIN
            key = int(card_pin)
        elif pin != None and card != None:
            type = self.ACCESS_TYPE_CARD_AND_PIN
            key = int(card) ^ self.pack_pin(str(pin))
        elif card != None:
            type = self.ACCESS_TYPE_CARD
            key = int(card)
        elif pin != None:
            type = self.ACCESS_TYPE_PIN
            key = self.pack_pin(str(pin))
        else:
            type = self.ACCESS_TYPE_NONE
            key = 0
        access = type | ((bool(int(used)) & 1) << 3) | ((int(doors) & 0xF) << 4)
        return struct.pack("<LB", key, access)

    def set_access_record(self, index, pin = None, card = None,
                          used = False, doors = 0, **kwargs):
        req = struct.pack("<H", index)
        req += self._pack_access_record(pin, card, doors, used,
                                        kwargs.get('card+pin'))
        self.send_cmd(self.CMD_SET_ACCESS_RECORD, req, 0)
        return {}

    def set_access(self, pin = None, card = None, doors = 0):
        if pin == None and card == None:
            raise ValueError('No card number or pin given')
        req = self._pack_access_record(pin, card, doors)
        self.send_cmd(self.CMD_SET_ACCESS, req, 0)
        return {}

    @classmethod
    def _pack_access_record_v2_type(self, card_type, pin_type):
        rec_type = 0

        if card_type is None:
            pass
        elif card_type == 'id':
            rec_type |= self.ACCESS_RECORD_TYPE_CARD_ID
        else:
            raise ValueError('Invalid card type')

        if pin_type is None:
            pass
        elif pin_type == 'fixed':
            rec_type |= self.ACCESS_RECORD_TYPE_PIN_FIXED
        elif pin_type == 'hotp':
            rec_type |= self.ACCESS_RECORD_TYPE_PIN_HOTP
        elif pin_type == 'totp':
            rec_type |= self.ACCESS_RECORD_TYPE_PIN_TOTP
        else:
            raise ValueError('Invalid pin type')

        if rec_type == 0:
            raise ValueError('No card or pin given')

        return rec_type

    @classmethod
    def _pack_access_record_v2(self, card_type = None, pin_type = None,
                               doors = 0, used = False, card = None,
                               pin = None, otp_key = None,
                               otp_digits = 6, hotp_resync_limit = 1,
                               hotp_counter = 0, totp_interval = 60,
                               totp_allow_followings = 0,
                               totp_allow_previous = 0):
        if pin_type in ('totp', 'hotp'):
            if otp_digits < 6 or otp_digits > 10:
                raise ValueError('OTP digits must be between 6 and 10')
        if pin_type == 'totp':
            if totp_allow_followings < 0 or totp_allow_followings > 3:
                raise ValueError('TOTP allow followings must be between 0 and 3')
            if totp_allow_previous < 0 or totp_allow_previous > 3:
                raise ValueError('TOTP allow previous must be between 0 and 3')
            if totp_interval < 0:
                raise ValueError('TOTP interval must be >= 0')
        if pin_type == 'hotp':
            if hotp_resync_limit < 0 or hotp_resync_limit > 15:
                raise ValueError('HOTP resync limit must be between 0 and 15')
            if hotp_counter < 0:
                raise ValueError('HOTP counter must be >= 0')

        hdr = self._pack_access_record_v2_type(card_type, pin_type)
        hdr |= (bool(used) << 3) | ((int(doors) & 0xF) << 4)
        rec_hdr = struct.pack("B", hdr)

        if card_type is None:
            rec_card = struct.pack("<L", 0)
        elif card_type == 'id':
            rec_card = struct.pack("<L", int(card))
        else:
            raise ValueError('Invalid card type')

        if pin_type is None:
            rec_pin = struct.pack("<L", 0)
        elif pin_type == 'fixed':
            rec_pin = struct.pack("<L", self.pack_pin(str(pin)))
        elif pin_type in ('hotp', 'totp'):
            otp_cfg = otp_key & 0x3FF
            otp_cfg |= ((otp_digits - 6) & 3) << 10
            if pin_type == 'totp':
                otp_cfg |= (totp_allow_followings & 3) << 12
                otp_cfg |= (totp_allow_previous & 3) << 14
                c = totp_interval
            if pin_type == 'hotp':
                otp_cfg |= (hotp_resync_limit & 0xF) << 12
                c = hotp_counter
            rec_pin = struct.pack("<HH", otp_cfg, c)
        else:
            raise ValueError('Invalid PIN type')

        return rec_hdr + rec_card + rec_pin

    @since_version(3)
    def set_access_record_v2(self, index, **kwargs):
        req = struct.pack("<H", index)
        req += self._pack_access_record_v2(**kwargs)
        self.send_cmd(self.CMD_SET_ACCESS_RECORD_V2, req, 0)
        return {}

    @since_version(3)
    def set_access_v2(self, **kwargs):
        req = self._pack_access_record_v2(**kwargs)
        self.send_cmd(self.CMD_SET_ACCESS_V2, req, 0)
        return {}

    def get_access(self, pin = None, card = None):
        if pin == None and card == None:
            raise ValueError('No card number or pin given')
        req = self._pack_access_record(pin, card)
        response = self.send_cmd(self.CMD_GET_ACCESS, req, 1)
        return {
            'doors': struct.unpack('B', response[0:1])[0],
        }

    @classmethod
    def _pack_get_access_req_v2(self, card_type = None, pin_type = None,
                                card = None, pin = None, otp_key = None,
                                **kwargs):

        req_type = self._pack_access_record_v2_type(card_type, pin_type)

        if card_type is None:
            req_card = 0
        elif card_type == 'id':
            req_card = int(card)
        else:
            raise ValueError('Invalid card type')

        if pin_type is None:
            req_pin = 0
        elif pin_type == 'fixed':
            req_pin = self.pack_pin(pin)
        elif pin_type in ('totp', 'hotp'):
            req_pin = int(otp_key)
        else:
            raise ValueError('Invalid pin type')

        return struct.pack('<BLL', req_type, req_card, req_pin)

    @since_version(3)
    def get_access_v2(self, **kwargs):
        req = self._pack_get_access_req_v2(**kwargs)
        response = self.send_cmd(self.CMD_GET_ACCESS_V2, req, 1)
        return self._unpack_access_record_v2(response)

    def remove_all_access(self):
        self.send_cmd(self.CMD_REMOVE_ALL_ACCESS)
        return {}

    def _generate_used_access(self, clear):
        i = 0
        while True:
            req = struct.pack("<HB", int(i), clear)
            response = self.send_cmd(self.CMD_GET_USED_ACCESS, req, 7)
            i, = struct.unpack("<H", response[0:2])
            rec = self._unpack_access_record(response[2:])
            if 'doors' not in rec:
                return
            rec['index'] = i
            yield rec
            i = i + 1

    def get_used_access(self, clear = False):
        return {
            'used': list(self._generate_used_access(clear)),
        }

    def _generate_used_access_v2(self, clear):
        i = 0
        while True:
            req = struct.pack("<HB", int(i), clear)
            try:
                response = self.send_cmd(self.CMD_GET_USED_ACCESS_V2, req, 11)
            except AVRDoorCtrlError as err:
                if err.errno == AVRDoorCtrlError.ENOENT:
                    return
                raise
            i, = struct.unpack("<H", response[0:2])
            rec = self._unpack_access_record_v2(response[2:])
            rec['index'] = i - 1
            yield rec

    @since_version(3)
    def get_used_access_v2(self, clear = False):
        return {
            'used': list(self._generate_used_access_v2(clear)),
        }

    def get_time(self):
        response = self.send_cmd(self.CMD_GET_TIME, None, 4)
        tm, = struct.unpack("<L", response[0:4])
        return {
            "time": time.asctime(time.gmtime(tm)),
        }

    def set_time(self, val = None):
        if val is not None:
            val = int(val)
        if val is None or val <= 0:
            val = calendar.timegm(time.gmtime())
        req = struct.pack("<L", val)
        self.send_cmd(self.CMD_SET_TIME, req, 0)
        return {}

    def parse_event(self, type, payload):
        if not self.is_event(type):
            raise TypeError
        if type == self.EVENT_STARTED:
            return {
                'event': 'started',
            }
        elif type == self.EVENT_ACCESS:
            access, card, pin = struct.unpack("<BLL", payload)
            ev = {
                'event': 'access',
                'door': access & 0xF,
                'type': self.access_record_types[(access >> 4) & 0x3],
                'granted': (access >> 6) & 0x1,
            }
            if ev['type'] in ('pin', 'card+pin'):
                ev['pin'] = self.unpack_pin(pin)
            if ev['type'] in ('card', 'card+pin'):
                ev['card'] = card
            return ev
        elif type == self.EVENT_DOOR_STATUS:
            state = struct.unpack("<B", payload)
            return {
                'door': state & 0xF,
                'open': (state >> 4) & 1,
            }
        else:
            return {
                'event': 'unknown',
                'type': type,
            }

class AVRDoorCtrlUbusHandler(ubus.UObject):
    def __init__(self, url, username, password,
                 uobject = None, **ubus_kwargs):
        if uobject is None:
            url, uobject = urldefrag(url)
        if not uobject:
            raise ValueError("No object given")
        bus = ubus.UBus(url, username, password, **ubus_kwargs)
        super(AVRDoorCtrlUbusHandler, self).__init__(bus, uobject)

    @staticmethod
    def _fix_card_n_pin(rec):
        if 'card+pin' in rec and rec['card+pin'] < 0:
            rec['card+pin'] = struct.unpack(
                    '<L', struct.pack('<l', rec['card+pin']))

    @ubus.method
    def get_device_descriptor(self):
        pass

    @ubus.method
    def get_door_config(self, index: int):
        pass

    @ubus.method
    def set_door_config(self, index: int, open_time: int):
        pass

    @ubus.method
    def get_access_record(self, index: int):
        rec = self.call('get_access_record', index = index)
        self._fix_card_n_pin(rec)
        return rec

    @ubus.method
    def set_access_record(self, index: int, pin: str = None,
                          card: int = None, used: bool = False,
                          doors: int = 0):
        pass

    @ubus.method
    def set_access(self, pin: str = None, card: int = None, doors: int = 0):
        pass

    @ubus.method
    def get_access(self, pin: str = None, card: int = None):
        pass

    @ubus.method
    def remove_all_access(self):
        pass

    @ubus.method
    def get_used_access(self, clear: int = 0):
        resp = self.call('get_used_access', clear = clear)
        for used in resp['used']:
            self._fix_card_n_pin(used)
        return resp

class AVRDoorCtrl(object):
    """
    Proxy class that select an implementation depending on the type
    of device URI.
    """
    def __init__(self, uri, *args, **kwargs):
        if uri.startswith('/dev/tty'):
            self._handler = AVRDoorCtrlSerialHandler(uri, *args, **kwargs)
        elif uri.startswith('http://') or uri.startswith('https://'):
            self._handler = AVRDoorCtrlUbusHandler(uri, *args, **kwargs)
        else:
            raise ValueError('Unsupported URL type')

    def __getattr__(self, attr):
        return getattr(self._handler, attr)

    @staticmethod
    def optionalmethod(arg):
        def decorator(name, fallback):
            @functools.wraps(fallback)
            def wrapper(self, *args, **kwargs):
                try:
                    method = getattr(self._handler, name)
                except AttributeError:
                    return fallback(self, *args, **kwargs)
                try:
                    return method(*args, **kwargs)
                except NotImplementedError:
                    return fallback(self, *args, **kwargs)
            return wrapper
        # Support decorators without parenthesis
        if callable(arg):
            return decorator(arg.__name__, arg)
        else:
            return functools.partial(decorator, arg)

    @staticmethod
    def versionedmethod(field, default_version, name=None):
        def decorator(default_version, name, fallback):
            if name is None:
                name = fallback.__name__
            @functools.wraps(fallback)
            def wrapper(self, *args, **kwargs):
                version = kwargs.pop(field, default_version)
                try:
                   method = getattr(self, f'{name}_v{version}')
                except AttributeError:
                    return fallback(self, version, *args, **kwargs)
                return method(*args, **kwargs)
            return wrapper
        return functools.partial(decorator, default_version, name)

    @staticmethod
    def _access_record_to_v2(rec, from_version):
        if from_version == 1:
            # The old card+pin can't be converted back without having
            # either the card or pin.
            if 'card+pin' in rec:
                raise ValueError("Version 1 card+pin records can't be converted to version 2")
            if 'card' in rec:
                rec['card_type'] = 'id'
            if 'pin' in rec:
                rec['pin_type'] = 'fixed'
            return rec
        if from_version == 2:
            return rec
        raise ValueError(f"Can't convert record version {from_version} to version 2")

    @staticmethod
    def _access_record_to_v1(rec, from_version):
        if from_version == 1:
            return rec
        if from_version == 2:
            card_type = rec.pop('card_type', 'id')
            if card_type != 'id':
                raise ValueError(f"Card type {rec['card_type']} not supported by v1 records")
            pin_type = rec.pop('pin_type', 'fixed')
            if pin_type != 'fixed':
                raise ValueError(f"PIN type {rec['pin_type']} not supported by v1 records")
            return {k: v for k, v in rec.items() if k in ('doors', 'used', 'pin', 'card')}
        raise ValueError(f"Can't convert record version {from_version} to version 1")

    @optionalmethod
    def get_access_record_v2(self, index, **kwargs):
        # Fallback implementation when the handler doesn't support v2 records
        rec = self._handler.get_access_record(index, **kwargs)
        return self._access_record_to_v2(rec, from_version=1)

    @optionalmethod('get_access_record')
    def get_access_record_v1(self, index, **kwargs):
        # Fallback implementation when the handler doesn't support v1 records
        rec = self._handler.get_access_record_v2(index, **kwargs)
        return self._access_record_to_v1(rec, from_version=2)

    @versionedmethod('record_version', 2)
    def get_access_record(self, version, *args, **kwargs):
        raise ValueError(f'Record version {version} not supported')


    @optionalmethod
    def get_access_v2(self, **kwargs):
        # Fallback implementation when the handler doesn't support v2 records
        rec = self._handler.get_access(**kwargs)
        return self._access_record_to_v2(rec, from_version=1)

    @optionalmethod('get_access')
    def get_access_v1(self, **kwargs):
        # Fallback implementation when the handler doesn't support v1 records
        rec = self._handler.get_access_v2(**kwargs)
        return self._access_record_to_v1(rec, from_version=2)

    @versionedmethod('record_version', 2)
    def get_access(self, version, *args, **kwargs):
        # Fallback when the requested version is not found
        raise ValueError(f'Record version {version} not supported')


    @optionalmethod
    def set_access_record_v2(self, index, **kwargs):
        # Fallback implementation when the handler doesn't support v2 records
        rec = self._access_record_to_v1(kwargs, from_version=2)
        return self._handler.set_access_record(index, rec)

    @optionalmethod('set_access_record')
    def set_access_record_v1(self, index, **kwargs):
        # Fallback implementation when the handler doesn't support v1 records
        rec = self._access_record_to_v2(kwargs, from_version=1)
        return self._handler.set_access_record_v2(index, rec)

    @versionedmethod('record_version', 2)
    def set_access_record(self, version, *args, **kwargs):
        # Fallback when the requested version is not found
        raise ValueError(f'Record version {version} not supported')


    @optionalmethod
    def set_access_v2(self, **kwargs):
        # Fallback implementation when the handler doesn't support v2 records
        rec = self._access_record_to_v1(kwargs, from_version=2)
        return self._handler.set_access(rec)

    @optionalmethod('set_access_record')
    def set_access_v1(self, **kwargs):
        # Fallback implementation when the handler doesn't support v1 records
        rec = self._access_record_to_v2(kwargs, from_version=1)
        return self._handler.set_access_v2(rec)

    @versionedmethod('record_version', 2)
    def set_access(self, **kwargs):
        # Fallback when the requested version is not found
        raise ValueError(f'Record version {version} not supported')

    def get_all_access_records(self, record_version=2):
        desc = self.get_device_descriptor()
        acl = {}
        for i in range(desc["num_access_records"]):
            try:
                a = self.get_access_record(index=i, record_version=record_version)
            except AVRDoorCtrlError as err:
                if err.errno == AVRDoorCtrlError.EBUSY:
                    continue
                if err.errno == AVRDoorCtrlError.ENOENT:
                    continue
                raise
            if 'doors' in a:
                acl[i] = a
        return acl

    def set_all_access_records(self, acl, record_version=2):
        self.remove_all_access()
        for idx in acl:
            record = acl[idx]
            if 'index' not in record:
                record['index'] = idx
            self.set_access_record(**record, record_version=record_version)

    @optionalmethod
    def get_used_access_v2(self, *args, **kwargs):
        # Fallback implementation when the handler doesn't support v2 records
        resp = self._handler.get_used_access(**kwargs)
        return {
            'used': [self._access_record_to_v2(rec, from_version=1) for rec in resp['used']],
        }

    @optionalmethod('get_used_access')
    def get_used_access_v1(self, *args, **kwargs):
        # Fallback implementation when the handler doesn't support v2 records
        resp = self._handler.get_used_access_v2(**kwargs)
        return {
            'used': [self._access_record_to_v1(rec, from_version=2) for rec in resp['used']],
        }

    @versionedmethod('record_version', 2)
    def get_used_access(self, version, *args, **kwargs):
        # Fallback when the requested version is not found
        raise ValueError(f'Record version {version} not supported')

class AVRDoorCtrlTool(AVRDoorCtrl):
    def show_events(self):
        while True:
            try:
                print(self.read_event())
            except TimeoutError:
                pass
            except KeyboardInterrupt:
                break

    def backup_access_records(self, path, record_version=2):
        acl = self.get_all_access_records()
        fd = open(path, 'w')
        fd.write(json.dumps(acl, sort_keys=True, indent=4) + '\n')
        fd.close()
        return {}

    def restore_access_records(self, path, record_version=2):
        fd = open(path, 'r')
        acl = json.loads(fd.read())
        self.set_all_access_records(acl)
        return {}

    @classmethod
    def get_otp_key(cls, root_key, key_id, card=None):
        if HMAC is None:
            raise NotImplementedError
        root_key = base64.b16decode(root_key)
        info = struct.pack('>H', key_id)
        if card is not None:
            info += struct.pack('>L', card)
        info += struct.pack('B', 1)

        hmac = HMAC.new(root_key, info, digestmod=SHA1)
        key = hmac.digest()
        return key

    def access_record_add_otp_secret(self, rec, root_key):
        if root_key is None:
            return rec
        if rec.get('pin_type') not in ('hotp', 'totp'):
            return rec
        try:
            key = self.get_otp_key(root_key, rec.get('otp_key'),
                                   rec.get('card'))
        except NotImplementedError:
            pass
        else:
            rec['otp_secret'] = base64.b32encode(key).decode('ascii')
        return rec

    def get_access_record(self, *args, root_key=None, **kwargs):
        rec = super().get_access_record(*args, **kwargs)
        return self.access_record_add_otp_secret(rec, root_key)

    def get_access(self, *args, root_key=None, **kwargs):
        rec = super().get_access(*args, **kwargs)
        return self.access_record_add_otp_secret(rec, root_key)

def add_parser_arguments_access_record(method_parser):
    method_parser.add_argument(
        '--record-version', type = int, default = 2,
        help = 'Access record version to return')
    method_parser.add_argument(
        '--card-type', help = 'Card type')
    method_parser.add_argument(
        '--card', type = int, help = 'Card number')
    method_parser.add_argument(
        '--pin-type', help = 'PIN type')
    method_parser.add_argument(
        '--pin', help = 'PIN with 1 to 8 digits')
    method_parser.add_argument(
        '--otp-key', type = int,
        help = 'Index of the OTP key for HOTP and TOTP PIN')

def add_parser_arguments_access_record_properties(method_parser):
    method_parser.add_argument(
        '--otp-digits', type = int, default = 6,
        help = 'Number of digits for H/TOTP PIN')
    method_parser.add_argument(
        '--hotp-resync-limit', type = int, default = 1,
        help = 'Upper limit for re-syncing the HOTP counter')
    method_parser.add_argument(
        '--hotp-counter', type = int, default = 0,
        help = 'Current value of the HOTP counter')
    method_parser.add_argument(
        '--totp-interval', type = int, default = 60,
        help = 'Interval in minutes for TOTP PIN')
    method_parser.add_argument(
        '--totp-allow-followings', type = int, default = 0,
        help = 'Number of following PIN to allow for H/TOTP')
    method_parser.add_argument(
        '--totp-allow-previous', type = int, default = 1,
        help = 'Number of previous PIN to allow for H/TOTP')
    method_parser.add_argument(
        '--used', action='store_true', help = 'Mark the record as used')
    method_parser.add_argument(
        '--doors', type = int, required = True,
        help = 'Bitmask of the doors that can be opened')

if __name__ == '__main__':
    import binascii, argparse, sys

    # Main parser
    parser = argparse.ArgumentParser(
        description='Low level tool for the AVR Door Controllers')
    parser.add_argument('url', metavar = 'URL')

    parser.add_argument(
        '--timeout', type = int, help = 'Timeout for serial or UBus access')
    parser.add_argument(
        '--username', help = 'Username for UBus access')
    parser.add_argument(
        '--password', help = 'Password for UBus access')
    parser.add_argument(
        '--log-level', choices=('DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL'),
        help = 'Log level', default='WARNING')
    parser.add_argument(
        '--controller-version', help = 'Force the UART protocol version')

    # Method parsers
    method_subparsers = parser.add_subparsers(dest='method')

    method_parser = method_subparsers.add_parser(
        'get_device_descriptor', help = 'Get the device descriptor')

    method_parser = method_subparsers.add_parser(
        'get_controller_config', help = 'Get the controller configuration')

    method_parser = method_subparsers.add_parser(
        'set_controller_config', help = 'Set the controller configuration')
    method_parser.add_argument(
        '--root-key', help = 'The controller root key in hexadecial (20 bytes)')

    method_parser = method_subparsers.add_parser(
        'get_door_config', help = 'Get a door configuration')
    method_parser.add_argument(
        '--index', type = int, required = True,
        help = 'Door index')

    method_parser = method_subparsers.add_parser(
        'set_door_config', help = 'Set a door configuration')
    method_parser.add_argument(
        '--index', type = int, required = True,
        help = 'Door index')
    method_parser.add_argument(
        '--open_time', metavar = 'TIME', type = int, required = True,
        help = 'Time to keep the door open, in milliseconds')

    method_parser = method_subparsers.add_parser(
        'get_access_record', help = 'Get an access record')
    method_parser.add_argument(
        '--index', type = int, required = True,
        help = 'Access record index')
    method_parser.add_argument(
        '--record-version', type = int, default = 2,
        help = 'Access record version to return')
    method_parser.add_argument(
        '--root-key', help = 'The controller root key in hexadecimal')

    method_parser = method_subparsers.add_parser(
        'set_access_record', help = 'Set an access record')
    method_parser.add_argument(
        '--index', type = int, required = True,
        help = 'Access record index')
    add_parser_arguments_access_record(method_parser)
    add_parser_arguments_access_record_properties(method_parser)

    method_parser = method_subparsers.add_parser(
        'set_access', help = 'Add or remove access to a card and/or pin')
    add_parser_arguments_access_record(method_parser)
    add_parser_arguments_access_record_properties(method_parser)

    method_parser = method_subparsers.add_parser(
        'get_access', help = 'Get the access for a card and/or pin')
    add_parser_arguments_access_record(method_parser)
    method_parser.add_argument(
        '--root-key', help = 'The controller root key in hexadecimal')

    method_parser = method_subparsers.add_parser(
        'get_used_access',
        help = 'Get all the used access records')
    method_parser.add_argument(
        '--clear', action='store_true',
        help = 'Clear the used flags while going through the records')
    method_parser.add_argument(
        '--record-version', type = int, default = 2,
        help = 'Access record version to return')

    method_parser = method_subparsers.add_parser(
        'remove_all_access', help = 'Erase all access records')

    method_parser = method_subparsers.add_parser(
        'show_events', help = 'Show the events received from the controller')

    method_parser = method_subparsers.add_parser(
        'backup_access_records',
        help = 'Backup all the access records to file')
    method_parser.add_argument(
        '--record-version', type = int, default = 2,
        help = 'Access record version to return')
    method_parser.add_argument(
        'path', help = 'File to save the access records to')

    method_parser = method_subparsers.add_parser(
        'restore_access_records',
        help = 'Restore all the access records from a backup file')
    method_parser.add_argument(
        '--record-version', type = int, default = 2,
        help = 'Access record version to return')
    method_parser.add_argument(
        'path', help = 'File to read the access records from')

    method_parser = method_subparsers.add_parser(
        'get_time',
        help = 'Get the time from the controller')

    method_parser = method_subparsers.add_parser(
        'set_time',
        help = 'Set the time from the controller')
    method_parser.add_argument(
        'val', help = 'Time to set on the device, 0 for now')

    args = parser.parse_args()

    log_level = getattr(logging, args.log_level.upper())
    del args.log_level

    url = args.url
    del args.url

    method = args.method
    del args.method

    url_kwargs = {}
    for f in [ 'timeout', 'username', 'password', 'controller_version' ]:
        v = getattr(args, f)
        if v is not None:
            url_kwargs[f] = v
        delattr(args, f)

    if method in [ 'set_access_record', 'set_access', 'get_access' ] and \
       args.record_version == 2:
        if args.card_type is None and args.card is not None:
            args.card_type = 'id'
        if args.pin_type is None and args.pin is not None:
            args.pin_type = 'fixed'
        if args.pin_type is None and args.otp_key is not None:
            args.pin_type = 'totp'

    logging.basicConfig(level=log_level, format='%(levelname)s: %(message)s')
    door = AVRDoorCtrlTool(url, **url_kwargs)
    try:
        result = getattr(door, method).__call__(**vars(args))
    except AVRDoorCtrlError as err:
        result = {'errno': err.errno, 'error': str(err)}
    print(json.dumps(result))
    sys.exit(result.get('errno', 0))
