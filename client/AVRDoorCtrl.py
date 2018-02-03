#!/usr/bin/env python3

import serial
import termios
import struct
import crcmod
import ubus
import json
from urllib.parse import urldefrag

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
            # TODO: raise timeout if nothing is available
            c = self._tty.read(1)
            if len(c) < 1:
                raise ValueError
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

class AVRDoorCtrlSerialHandler(object):
    CMD_GET_DEVICE_DESCRIPTOR = 0
    CMD_GET_DOOR_CONFIG = 10
    CMD_SET_DOOR_CONFIG = 11
    CMD_GET_ACCESS_RECORD = 20
    CMD_SET_ACCESS_RECORD = 21
    CMD_SET_ACCESS = 22
    CMD_REMOVE_ALL_ACCESS = 23
    CMD_GET_ACCESS = 24

    EVENT_BASE = 127
    EVENT_STARTED = EVENT_BASE + 0

    REPLY_OK = 0
    REPLY_ERROR = 255

    access_record_types = ("none", "pin", "card", "pin+card")

    ACCESS_TYPE_NONE = 0
    ACCESS_TYPE_PIN = 1
    ACCESS_TYPE_CARD = 2
    ACCESS_TYPE_CARD_AND_PIN = ACCESS_TYPE_CARD | ACCESS_TYPE_PIN

    def __init__(self, dev, *args, **kwargs):
        if dev.startswith('/dev/tty'):
            self._transport = AVRDoorCtrlUartTransport(dev, *args, **kwargs)
        else:
            raise ValueError
        try:
            type, payload = self.read_msg()
            if type != self.EVENT_STARTED:
                print("Warning: Got bad start event\n")
        except:
            pass

    def read_msg(self):
        type, payload = self._transport.read_msg()
        #print("Got %02x - %s" % (type, payload))
        return type, payload

    def send_msg(self, type, payload = None):
        #print("W: %02x - %s" % (type, payload))
        self._transport.send_msg(type, payload)

    def send_cmd(self, type, payload = None, response_size=0):
        self.send_msg(type, payload)
        reply = self.EVENT_BASE
        # Read the incoming messages, but skip over events
        while reply >= self.EVENT_BASE and reply != self.REPLY_ERROR:
            reply, response = self.read_msg()
        # Raise errors
        if reply == self.REPLY_ERROR:
            if len(response) < 1:
                raise Exception("Invalid error reply")
            errno, = struct.unpack("b", response)
            raise Exception("Error %d" % -errno)
        # Make sure we got an OK
        if reply != self.REPLY_OK:
            raise Exception("Bad reply type: %d" % reply)
        # Make sure we got enough data
        if len(response) < response_size:
            raise Exception("Bad response length: %d" % len(response))
        return response

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

    def get_device_descriptor(self):
        response = self.send_cmd(
            self.CMD_GET_DEVICE_DESCRIPTOR, None, 5)
        major, minor, doors, records = struct.unpack("<BBBH", response[0:5])
        ret = {
            "version": "%d.%d" % (major, minor),
            "num_doors": doors,
            "num_access_records" : records,
        }
        if len(response) > 5:
            ret["free_access_records"], = struct.unpack("<H", response[5:7])
        return ret

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

    def get_access_record(self, index, pin = None, card = None):
        response = self.send_cmd(self.CMD_GET_ACCESS_RECORD,
                                 struct.pack("<H", int(index)), 5)
        key, access = struct.unpack("<LB", response[0:5])
        # If the record is invalid ignore it
        if access & (1 << 2):
            access = 0
        type = access & 0x3
        doors = (access >> 4) & 0xF
        ret = {
            "index": index,
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

    @classmethod
    def _pack_access_record(self, pin = None, card = None,
                            doors = 0, card_pin = None):
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
        access = type | ((int(doors) & 0xF) << 4)
        return struct.pack("<LB", key, access)

    def set_access_record(self, index, pin = None, card = None,
                          doors = 0, **kwargs):
        req = struct.pack("<H", index)
        req += self._pack_access_record(pin, card, doors,
                                        kwargs.get('card+pin'))
        self.send_cmd(self.CMD_SET_ACCESS_RECORD, req, 0)
        return {}

    def set_access(self, pin = None, card = None, doors = 0):
        if pin == None and card == None:
            raise ValueError
        req = self._pack_access_record(pin, card, doors)
        self.send_cmd(self.CMD_SET_ACCESS, req, 0)
        return {}

    def get_access(self, pin = None, card = None):
        if pin == None and card == None:
            raise ValueError
        req = self._pack_access_record(pin, card)
        response = self.send_cmd(self.CMD_GET_ACCESS, req, 1)
        return {
            'doors': struct.unpack('B', response[0:1])[0],
        }

    def remove_all_access(self):
        self.send_cmd(self.CMD_REMOVE_ALL_ACCESS)
        return {}

class AVRDoorCtrlUbusHandler(ubus.UObject):
    def __init__(self, url, username, password,
                 uobject = None, **ubus_kwargs):
        if uobject is None:
            url, uobject = urldefrag(url)
        if not uobject:
            raise ValueError("No object given")
        bus = ubus.UBus(url, username, password, **ubus_kwargs)
        super(AVRDoorCtrlUbusHandler, self).__init__(bus, uobject)

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
        pass

    @ubus.method
    def set_access_record(self, index: int, pin: str = None,
                          card: int = None):
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

    def get_all_access_records(self):
        desc = self.get_device_descriptor()
        acl = {}
        for i in range(desc["num_access_records"]):
            a = self.get_access_record(index=i)
            if 'doors' in a:
                acl[i] = a
        return acl

    def backup_access_records(self, path):
        acl = self.get_all_access_records()
        fd = open(path, 'w')
        fd.write(json.dumps(acl, sort_keys=True, indent=4) + '\n')
        fd.close()

    def set_all_access_records(self, acl):
        self.remove_all_access()
        for idx in acl:
            record = acl[idx]
            if 'index' not in record:
                record['index'] = idx
            self.set_access_record(**record)

    def restore_access_records(self, path):
        fd = open(path, 'r')
        acl = json.loads(fd.read())
        self.set_all_access_records(acl)

if __name__ == '__main__':
    import binascii, argparse

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

    # Method parsers
    method_subparsers = parser.add_subparsers(dest='method')

    method_parser = method_subparsers.add_parser(
        'get_device_descriptor', help = 'Get the device descriptor')

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

    method_parser = method_subparsers.add_parser(
        'set_access_record', help = 'Set an access record')
    method_parser.add_argument(
        '--index', type = int, required = True,
        help = 'Access record index')
    method_parser.add_argument(
        '--card', type = int, help = 'Card number')
    method_parser.add_argument(
        '--pin', help = 'PIN with 1 to 8 digits')
    method_parser.add_argument(
        '--doors', type = int, required = True,
        help = 'Bitmask of the doors that can be opened')

    method_parser = method_subparsers.add_parser(
        'set_access', help = 'Add or remove access to a card and/or pin')
    method_parser.add_argument(
        '--card', type = int, help = 'Card number')
    method_parser.add_argument(
        '--pin', help = 'PIN with 1 to 8 digits')
    method_parser.add_argument(
        '--doors', type = int, required = True,
        help = 'Bitmask of the doors that can be opened with this card and/or pin')

    method_parser = method_subparsers.add_parser(
        'get_access', help = 'Get the access for a card and/or pin')
    method_parser.add_argument(
        '--card', type = int, help = 'Card number')
    method_parser.add_argument(
        '--pin', help = 'PIN with 1 to 8 digits')

    method_parser = method_subparsers.add_parser(
        'remove_all_access', help = 'Erase all access records')

    method_parser = method_subparsers.add_parser(
        'show_events', help = 'Show the events received from the controller')

    method_parser = method_subparsers.add_parser(
        'backup_access_records',
        help = 'Backup all the access records to file')
    method_parser.add_argument(
        'path', help = 'File to save the access records to')

    method_parser = method_subparsers.add_parser(
        'restore_access_records',
        help = 'Restore all the access records from a backup file')
    method_parser.add_argument(
        'path', help = 'File to read the access records from')

    args = parser.parse_args()

    url = args.url
    del args.url

    method = args.method
    del args.method

    url_kwargs = {}
    for f in [ 'timeout', 'username', 'password' ]:
        v = getattr(args, f)
        if v is not None:
            url_kwargs[f] = v
        delattr(args, f)

    door = AVRDoorCtrl(url, **url_kwargs)

    if method == 'show_events':
        while True:
            try:
                cmd, data = door.read_msg()
                if len(data) > 0:
                    print("Command: %02x - %s" % (cmd, binascii.hexlify(data)))
                else:
                    print("Command: %02x" % cmd)
            except IndexError:
                pass
    else:
        print(getattr(door, method).__call__(**vars(args)))
