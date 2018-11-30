#!/usr/bin/env python3

import MySQLdb as dbapi2
import AVRDoorsDB
import AVRDoorCtrl

class Controller(AVRDoorsDB.Controller):
    def __init__(self, *args, **kwargs):
        super(Controller, self).__init__(*args, **kwargs)
        self._device = None

    @property
    def device(self):
        if self._device is None:
            if self.url is None:
                raise ValueError("Controller has no URL")
            args = {}
            if self.username is not None:
                args['username'] = self.username
                if self.password is not None:
                    args['password'] = self.password
            self._device = AVRDoorCtrl.AVRDoorCtrl(self.url, **args)
        return self._device

    def update_from_device_descriptor(self):
        desc = self.device.get_device_descriptor()
        self.firmware = desc["version"]
        self.max_acl = desc["num_access_records"]
        self.save()

    def set_access(self, card = None, pin = None, doors = 0):
        args = { 'doors': doors }
        if card != None:
            args['card'] = card
        if pin != None:
            args['pin'] = pin
        # Delete any old record, this is always needed as we have
        # no proper primary key because card or pin could be null
        cursor = self._db.cursor()
        cursor.execute(
            "delete from ControllerSetACL where " +
            "ControllerID = %s and Card <=> %s and PIN <=> %s",
            (self.id, card, pin));
        # If that wasn't deleting the record add it to the list
        if doors > 0:
            cursor.execute(
                "insert into ControllerSetACL set " +
                "ControllerID = %s, Card = %s, PIN = %s, Doors = %s",
                (self.id, card, pin, doors));
        # Apply the changes on the device and commit to the DB
        try:
            self.device.set_access(**args)
        except:
            self._db.rollback()
            raise
        else:
            self._db.commit()

    def describe_acl(self, card, pin, doors_mask):
        if card is not None:
            try:
                who = AVRDoorsDB.User(self._db, card, "Card = %s").name
            except:
                who = "card %s" % card
        else:
            who = "all"
        if pin is not None:
            who += " with pin %s" % pin
        doors = []
        idx = 0
        while doors_mask != 0:
            if doors_mask & 1:
                try:
                    name = self.get_door(idx).location
                except:
                    name = "door %d" % idx
                doors.append(name)
            doors_mask >>= 1
            idx += 1
        return (" and ".join(doors), who)

    def update_acl(self, reset = False, dry_run = False):
        # Check if we can access the controller, if not there is no point
        # in trying to update any ACL.
        try:
            if dry_run is False:
                self.device.get_device_descriptor()
        except Exception as err:
            msg = str(err) or type(err).__name__
            print("Skipping %s, controller is not accessible: %s" %
                  (self.location, msg))
            return
        # Get the list of changes to apply, sort by doors for the presentation
        cursor = self._db.cursor()
        if reset is True:
            print("Removing all ACL on %s" % self.location)
            if dry_run is True:
                print("WARNING: Controller reset can't be simulated")
            else:
                cursor.execute("delete from ControllerSetACL where " +
                               "ControllerID = %s",
                               (self.id,))
                try:
                    self.device.remove_all_access()
                except:
                    print("Failed to reset controller")
                    self._db.rollback()
                else:
                    self._db.commit()
        cursor.execute(
            "select Op, Card, PIN, Doors from ControllerChanges " +
            "where ControllerID = %s order by Op, Doors, Card, PIN",
            (self.id,));
        # Apply each access update
        last_access = None
        for add, card, pin, doors in cursor:
            # Convert to int to allow bit operations
            doors = int(doors)
            add = int(add)
            access, who = self.describe_acl(card, pin, doors)
            if access != last_access:
                print("%s:" % access)
            last_access = access
            try:
                if dry_run is False:
                    self.set_access(card, pin, add * doors)
            except Exception as e:
                op = "add" if add else "remove"
                print("\t* Failed to %s %s: %s" % (op, who, e))
            else:
                op = "Added" if add else "Removed"
                print("\t* %s %s" % (op, who))

class Actions(object):
    def __init__(self, db):
        self.db = db

    def print_list_header(self):
        pass

    def print_list_entry(self, obj):
        pass

    def list(self):
        self.print_list_header()
        for o in self.cls.get_all(self.db):
            self.print_list_entry(o)

    def show(self, identifier):
        o = self.cls(self.db, identifier)
        self.show_instance(o)

    def create(self, **kwargs):
        o = self.cls(self.db)
        for arg in kwargs:
             if kwargs[arg] is not None:
                 setattr(o, arg, kwargs[arg])
        o.save()
        return o

    def add(self, **kwargs):
        o = self.create(**kwargs)
        self.show(o.id)

    def update(self, identifier, **kwargs):
        o = self.cls(self.db, identifier)
        for arg in kwargs:
            val = kwargs[arg]
            if arg.startswith('delete_'):
                if val is True:
                    setattr(o, arg[7:], None)
            elif val is not None:
                setattr(o, arg, val)
        o.save()
        self.show(o.id)

    def delete(self, identifier):
        o = self.cls(self.db, identifier)
        o.delete()

class GroupActions(Actions):
    cls = AVRDoorsDB.Group

    def print_list_header(self):
        print(" %-4s | %-30s | %-40s | %s" %
              ("GID", "Groupname", "EMail", "Users"))
        print("-" * 6 + "+" + "-" * 32 + "+" + "-" * 42 + "+" + "-" * 10)

    def print_list_entry(self, grp):
        email = grp.email if grp.email is not None else ''
        print(" % 4d | %-30s | %-40s | %d" %
              (grp.id, grp.name, email, len(grp.users)))

    def show_instance(self, grp):
        print("Group ID:\t%d" % grp.id)
        print("Name:\t\t%s" % grp.name)
        if grp.email is not None:
            print("EMail:\t\t%s" % grp.email)
        print("Created On:\t%s" % grp.created_on)
        print("Last Modified:\t%s" % grp.last_modified)
        if len(grp.doors) > 0:
            print("Access:")
            for a in grp.doors:
                print("\t%s" % a.describe_to())
        if len(grp.users) > 0:
            print("Users:")
            for gu in grp.users:
                print("\t%s" % gu)

    def create(self, users = None, admin_users = None,
               doors = None, admin_doors = None, **kwargs):
        group = super().create(**kwargs)
        if users is not None:
            for user in users:
                group.add_user(user)
        if admin_users is not None:
            for user in admin_users:
                group.add_user(user, True)
        if doors is not None:
            for d in doors:
                group.add_access(d)
        if admin_doors is not None:
            for d in admin_doors:
                group.add_access(d, admin = True)
        return group

    def add_users(self, group, users, **kwargs):
        group = self.cls(self.db, group)
        for user in users:
            group.add_user(user, **kwargs)

    def update_users(self, group, users, **kwargs):
        group = self.cls(self.db, group)
        for user in users:
            group.update_user(user, **kwargs)

    def remove_users(self, group, users):
        group = self.cls(self.db, group)
        for user in users:
            group.remove_user(user)

class UserActions(Actions):
    cls = AVRDoorsDB.User

    def print_list_header(self):
        print(" %-4s | %-30s | %-40s | %-10s | %s" %
              ("UID", "Username", "EMail", "Card", "Groups"))
        print("-" * 6 + "+" + "-" * 32 + "+" + "-" * 42 + "+" + "-" * 20)

    def print_list_entry(self, u):
        email = u.email if u.email is not None else ''
        groups = ", ".join((gu.group.name for gu in u.groups))
        print("% 5d | %-30s | %-40s | %10s | %s" %
              (u.id, u.name, email, u.card, groups))

    def show_instance(self, user):
        print("User ID:\t%d" % user.id)
        print("Name:\t\t%s" % user.name)
        print("Password:\t%s" %
              ("Set" if user.password is not None else "Not Set"))
        if user.email is not None:
            print("EMail:\t\t%s" % user.email)
        if user.card is not None:
            print("Card:\t\t%s" % user.card)
        print("Created On:\t%s" % user.created_on)
        print("Last Modified:\t%s" % user.last_modified)
        if len(user.groups) > 0:
            groups = []
            for gu in user.groups:
                g = gu.group.name
                if gu.admin:
                    g += " (ADMIN)"
                groups.append(g)
            print("Groups:\t\t%s" % (", ".join(groups),))
        if len(user.access) > 0:
            print("Access:")
            for a in user.access:
                print("\t%s" % a.describe_to())

    def create(self, groups = None, admin_groups = None,
               doors = None, admin_doors = None, **kwargs):
        u = super(UserActions, self).create(**kwargs)
        if groups is not None:
            for g in groups:
                u.add_to_group(g)
        if admin_groups is not None:
            for g in admin_groups:
                u.add_to_group(g, True)
        if doors is not None:
            for d in doors:
                u.add_access(d)
        if admin_doors is not None:
            for d in admin_doors:
                u.add_access(d, admin = True)
        return u

    def add_to_group(self, user, groups, admin = False):
        user = self.cls(self.db, user)
        for grp in groups:
            user.add_to_group(grp, admin)
        self.show_instance(user)

    def remove_from_group(self, user, groups):
        user = self.cls(self.db, user)
        for grp in groups:
            user.remove_from_group(grp)
        self.show_instance(user)

class ControllerActions(Actions):
    cls = Controller

    def print_list_header(self):
        print(" %-4s | %-30s | %-5s | %s" %
              ("ID", "Location", "Doors", "Max ACL"))
        print("-" * 6 + "+" + "-" * 32 + "+" + "-" * 7 + "+" + "-" * 8)

    def print_list_entry(self, c):
        max_acl = "%4d" % c.max_acl if c.max_acl is not None else ''
        print("% 5d | %-30s | % 5d | %s" %
              (c.id, c.location, len(c.doors), max_acl))

    def show_instance(self, ctrl):
        print("Controller ID:\t%d" % ctrl.id)
        print("Location:\t%s" % ctrl.location)
        if ctrl.url is not None:
            print("URL:\t\t%s" % ctrl.url)
        if ctrl.username is not None:
            print("Username:\t%s" % ctrl.username)
            if ctrl.password is not None:
                print("Password:\t%s" % ctrl.password)
        if ctrl.firmware is not None:
            print("Firmware:\t%s" % ctrl.firmware)
        if ctrl.max_acl is not None:
            print("Max ACL:\t%d" % ctrl.max_acl)

    def update_from_device_descriptor(self, identifier):
        c = self.cls(self.db, identifier)
        print("Updating %s controller from device descriptor" % c.location)
        c.update_from_device_descriptor()
        self.show_instance(c)

    def update_acl(self, devices, override, reset, dry_run):
        # Create a patch list from the override arguments
        patches = {}
        if override != None:
            for ctrl, prop, val in override:
                ctrl = self.cls(self.db, ctrl)
                if ctrl.id not in patches:
                    patches[ctrl.id] = {}
                patches[ctrl.id][prop] = val
        # Get the devices to update if none was given
        if len(devices) == 0:
            cursor = self.db.cursor()
            cursor.execute(
                "select ControllerID from ControllerACLUpdates " +
                "group by ControllerID")
            for cid, in cursor:
                devices.append(cid)
        # Update all devices
        for d in devices:
            ctrl = self.cls(self.db, d)
            if ctrl.id in patches:
                for prop in patches[ctrl.id]:
                    setattr(ctrl, prop, patches[ctrl.id][prop])
            ctrl.update_acl(reset, dry_run)

class DoorActions(Actions):
    cls = AVRDoorsDB.Door

    def print_list_header(self):
        print(" %-4s | %-30s | %-30s | %-5s" %
              ("ID", "Location", "Controller", "Port"))
        print("-" * 6 + "+" + "-" * 32 + "+" + "-" * 32 + "+" + "-" * 7)

    def print_list_entry(self, d):
        ctrl = d.controller.location if d.controller is not None else ''
        port = "% 3d" % d.index if d.index is not None else ''
        print("% 5d | %-30s | %-30s | %s" %
              (d.id, d.location, ctrl, port))

    def show_instance(self, door):
        print("Door ID:\t%d" % door.id)
        print("Location:\t%s" % door.location)
        if door.controller is not None:
            print("Controller:\t%s" % door.controller.location)
        if door.index is not None:
            print("Port:\t\t%d" % door.index)
        if len(door.access) > 0:
            print("Access Records:")
            for a in door.access:
                print("\t%s" % a.describe_who())

    def add_access(self, door, user, group, pin, since, until, admin):
        door = self.cls(self.db, door)
        door.add_access(user, group, pin, since, until, admin)

    def update_access(self, door, user, group, pin, new_pin, since, until, admin):
        door = self.cls(self.db, door)
        door.update_access(user, group, pin, new_pin, since, until, admin)

    def remove_access(self, door, user, group, pin):
        door = self.cls(self.db, door)
        door.remove_access(user, group, pin)

if __name__ == '__main__':
    import argparse
    import sys

    # Main parser
    parser = argparse.ArgumentParser(
        description='Admin tool for the AVR Door Controllers')
    parser.add_argument(
        '--db', metavar = 'DB', type = str, default='AVRDoors')
    parser.add_argument(
        '--db-host', metavar = 'HOST', type = str)
    parser.add_argument(
        '--db-user', metavar = 'USER', type = str)
    parser.add_argument(
        '--db-password', metavar = 'PASSWORD', type = str)
    main_subparsers = parser.add_subparsers(dest='cls')

    #
    # Users
    #
    main_subparser = main_subparsers.add_parser('user')
    action_subparsers = main_subparser.add_subparsers(dest='action')

    # List the groups
    subparser = action_subparsers.add_parser(
        'list', help = 'List all the users')

    # Show a user details
    subparser = action_subparsers.add_parser(
        'show', help = 'Show the details of a user')
    subparser.add_argument(
        'identifier', metavar = 'USER', type = str,
        help = 'Name or ID of the user')

    # Add a user
    subparser = action_subparsers.add_parser(
        'add', help = 'Add a new user')
    subparser.add_argument(
        'name', metavar = 'NAME', type = str,
        help = 'Name of the user')
    subparser.add_argument(
        '--email', metavar = 'EMAIL', type = str, required = False,
        help = 'Contact EMail for the user')
    subparser.add_argument(
        '--phone', metavar = 'PHONE', type = str, required = False,
        help = 'Phone number of the user')
    subparser.add_argument(
        '--card', metavar = 'CARD', type = int, required = False,
        help = 'Card number of the user')
    subparser.add_argument(
        '--group', metavar = 'GROUP', type = str,
        required = False, action = 'append', dest = 'groups',
        help = 'Group the user should be added to')
    subparser.add_argument(
        '--group-admin', metavar = 'GROUP', type = str,
        required = False, action = 'append', dest = 'admin_groups',
        help = 'Group the user should be added to as admin')
    subparser.add_argument(
        '--door', metavar = 'DOOR', type = str,
        required = False, action = 'append', dest = 'doors',
        help = 'Give the user access to a door')
    subparser.add_argument(
        '--door-admin', metavar = 'DOOR', type = str,
        required = False, action = 'append', dest = 'admin_doors',
        help = 'Give the user access to a door as admin')

    # Update a user record
    subparser = action_subparsers.add_parser(
        'update', help = 'Update a user record')
    subparser.add_argument(
        'identifier', metavar = 'USER', type = str,
        help = 'Name or ID of the user')
    subparser.add_argument(
        '--name', metavar = 'NAME', type = str, required = False,
        help = 'Update the name of the user')
    subparser.add_argument(
        '--password', metavar = 'PASSWORD', required = False,
        help = 'Update the password of the user')
    subparser.add_argument(
        '--delete-password', action = 'store_true',
        help = 'Delete the password of the user, preventing login')
    subparser.add_argument(
        '--email', metavar = 'EMAIL', type = str, required = False,
        help = 'Update the e-mail of the user')
    subparser.add_argument(
        '--delete-email', action = 'store_true',
        help = 'Delete the e-mail of the user')
    subparser.add_argument(
        '--phone', metavar = 'PHONE', type = str, required = False,
        help = 'Update the phone number of the user')
    subparser.add_argument(
        '--delete-phone', action = 'store_true',
        help = 'Delete the phone number of the user')
    subparser.add_argument(
        '--card', metavar = 'CARD', type = int, required = False,
        help = 'Set the card number of the user')
    subparser.add_argument(
        '--delete-card', action = 'store_true',
        help = 'Delete the card of the user')

    # Remove a user
    subparser = action_subparsers.add_parser(
        'delete', help = 'Delete a user')
    subparser.add_argument(
        'identifier', metavar = 'USER', type = str,
        help = 'Name or ID of the user to delete')

    # Add a user to some groups
    subparser = action_subparsers.add_parser(
        'add-to-group', help = 'Add a user to one or more groups')
    subparser.add_argument(
        'user', metavar = 'USER', type = str,
        help = 'The user to add to the groups')
    subparser.add_argument(
        '--admin', action = 'store_true',
        help = 'Set the user as group admin')
    subparser.add_argument(
        'groups', metavar = 'GROUP', type = str, nargs = '+',
        help = 'The groups to add')

    # Add a user to some groups
    subparser = action_subparsers.add_parser(
        'remove-from-group', help = 'Remove a user from one or more groups')
    subparser.add_argument(
        'user', metavar = 'USER', type = str,
        help = 'The user to add to the groups')
    subparser.add_argument(
        'groups', metavar = 'GROUP', type = str, nargs = '+',
        help = 'The groups to add')

    #
    # Groups
    #
    main_subparser = main_subparsers.add_parser('group')
    action_subparsers = main_subparser.add_subparsers(dest='action')

    # List the groups
    subparser = action_subparsers.add_parser(
        'list', help = 'List all the groups')

    # Show a group details
    subparser = action_subparsers.add_parser(
        'show', help = 'Show the details of a group')
    subparser.add_argument(
        'identifier', metavar = 'NAME', type = str,
        help = 'Name or ID of the group')

    # Add a group
    subparser = action_subparsers.add_parser(
        'add', help = 'Add a new group of user')
    subparser.add_argument(
        'name', metavar = 'NAME', type = str,
        help = 'Name of the Group')
    subparser.add_argument(
        '--email', metavar = 'EMAIL', type = str, required = False,
        help = 'Contact EMail for the Group')
    subparser.add_argument(
        '--user', metavar = 'USER', type = str,
        required = False, action = 'append', dest = 'users',
        help = 'Add a user to the group')
    subparser.add_argument(
        '--admin-user', metavar = 'USER', type = str,
        required = False, action = 'append', dest = 'admin_users',
        help = 'Add an admin user to the group')
    subparser.add_argument(
        '--door', metavar = 'DOOR', type = str,
        required = False, action = 'append', dest = 'doors',
        help = 'Give the group access to a door')
    subparser.add_argument(
        '--door-admin', metavar = 'DOOR', type = str,
        required = False, action = 'append', dest = 'admin_doors',
        help = 'Give the group access to a door as admin')

    # Remove a group
    subparser = action_subparsers.add_parser(
        'delete', help = 'Delete a group of user')
    subparser.add_argument(
        'identifier', metavar = 'GROUP', type = str,
        help = 'Name or ID of the group to delete')

    # Add users to a group
    subparser = action_subparsers.add_parser(
        'add-users', help = 'Add one or more users to a group')
    subparser.add_argument(
        'group', metavar = 'GROUP', type = str,
        help = 'The group the user should be added to')
    subparser.add_argument(
        '--admin', action = 'store_true',
        help = 'Set the users as group admin')
    subparser.add_argument(
        'users', metavar = 'USER', type = str, nargs = '+',
        help = 'The users to add')

    # Update group users
    subparser = action_subparsers.add_parser(
        'update-users', help = 'Update one or more group users')
    subparser.add_argument(
        'group', metavar = 'GROUP', type = str,
        help = 'The group whose users should be updated')
    subparser.add_argument(
        '--admin', action = 'store_true',
        help = 'Set the users as group admin')
    subparser.add_argument(
        '--remove-admin', action = 'store_false', dest = 'admin',
        help = 'Set the users as group admin')
    subparser.add_argument(
        'users', metavar = 'USER', type = str, nargs = '+',
        help = 'The users to update')

    # Add a user to a group
    subparser = action_subparsers.add_parser(
        'remove-users', help = 'Remove one or more users from a group')
    subparser.add_argument(
        'group', metavar = 'GROUP', type = str,
        help = 'The group the users should be added to')
    subparser.add_argument(
        'users', metavar = 'USER', type = str, nargs = '+',
        help = 'The users to remove from the group')

    #
    # Controllers
    #
    main_subparser = main_subparsers.add_parser('controller')
    action_subparsers = main_subparser.add_subparsers(dest='action')

    # List all the controllers
    subparser = action_subparsers.add_parser(
        'list', help = 'List all the doors')

    # Show a controller details
    subparser = action_subparsers.add_parser(
        'show', help = 'Show the details of a controller')
    subparser.add_argument(
        'identifier', metavar = 'CTRL', type = str,
        help = 'Location or ID of the controller')

    # Add a controller
    subparser = action_subparsers.add_parser(
        'add', help = 'Add a new controller')
    subparser.add_argument(
        'location', metavar = 'LOCATION',
        help = 'Location of the controller')
    subparser.add_argument(
        '--url', metavar = 'URL', required = False,
        help = 'URL to access the controller')
    subparser.add_argument(
        '--username', metavar = 'USERNAME', required = False,
        help = 'Username to access the controller')
    subparser.add_argument(
        '--password', metavar = 'PASSWORD', required = False,
        help = 'Password to access the controller')
    subparser.add_argument(
        '--firmware', metavar = 'VERSION', required = False,
        help = 'Version of the controller firmware')
    subparser.add_argument(
        '--max-acl', metavar = 'NUM', type = int, required = False,
        help = 'Maximum number of ACL that can be stored by the controller')

    # Update a controller record
    subparser = action_subparsers.add_parser(
        'update', help = 'Update a controller')
    subparser.add_argument(
        'identifier', metavar = 'CONTROLLER',
        help = 'Location or ID of the controller')
    subparser.add_argument(
        '--location', metavar = 'LOCATION', required = False,
        help = 'Location of the controller')
    subparser.add_argument(
        '--url', metavar = 'URL', required = False,
        help = 'URL to access the controller')
    subparser.add_argument(
        '--delete-url', action = 'store_true',
        help = 'Delete the URL of the controller')
    subparser.add_argument(
        '--username', metavar = 'USERNAME', required = False,
        help = 'Update the username of the controller')
    subparser.add_argument(
        '--delete-username', action = 'store_true',
        help = 'Delete the username of the controller')
    subparser.add_argument(
        '--password', metavar = 'PASSWORD', required = False,
        help = 'Update the password of the controller')
    subparser.add_argument(
        '--delete-password', action = 'store_true',
        help = 'Delete the password of the controller')
    subparser.add_argument(
        '--firmware', metavar = 'VERSION', required = False,
        help = 'Update the version of the controller firmware')
    subparser.add_argument(
        '--delete-firmware', action = 'store_true',
        help = 'Delete the firmware version of the controller')
    subparser.add_argument(
        '--max-acl', metavar = 'NUM', type = int, required = False,
        help = 'Update the maximum number of ACL of the controller')
    subparser.add_argument(
        '--delete-max-acl', action = 'store_true',
        help = 'Delete the maximum number of ACL of the controller')

    # Remove a controller
    subparser = action_subparsers.add_parser(
        'delete', help = 'Delete a controller')
    subparser.add_argument(
        'identifier', metavar = 'CONTROLLER',
        help = 'Location or ID of the controller to delete')

    # Update the controllers
    subparser = action_subparsers.add_parser(
        'update-acl', help = 'Update the controllers ACL from the DB')
    subparser.add_argument(
        '--override', metavar = 'CONTROLLER', nargs = 3, action = 'append',
        help = 'Override a controller property')
    subparser.add_argument(
        '--reset', action = 'store_true',
        help = 'Reset the controller forcing a full update')
    subparser.add_argument(
        '--dry-run', action = 'store_true',
        help = 'Only shows what would be done')
    subparser.add_argument(
        'devices', metavar = 'CONTROLLER', type = str, nargs = '*',
        help = 'The devices to update, all if none given')

    #
    # Doors
    #
    main_subparser = main_subparsers.add_parser('door')
    action_subparsers = main_subparser.add_subparsers(dest='action')

    # List all the doors
    subparser = action_subparsers.add_parser(
        'list', help = 'List all the doors')

    # Show a group details
    subparser = action_subparsers.add_parser(
        'show', help = 'Show the details of a door')
    subparser.add_argument(
        'identifier', metavar = 'DOOR', type = str,
        help = 'Location or ID of the door')

    # Add a door
    subparser = action_subparsers.add_parser(
        'add', help = 'Add a new door')
    subparser.add_argument(
        'location', metavar = 'LOCATION', type = str,
        help = 'Location of the door')
    subparser.add_argument(
        '--controller', metavar = 'CONTROLLER', type = str,
        help = 'Controller the door is connected to')
    subparser.add_argument(
        '--port', metavar = 'PORT', type = int, dest = 'index',
        help = 'Controller port the door is connected to')

    # Update a user record
    subparser = action_subparsers.add_parser(
        'update', help = 'Update a door')
    subparser.add_argument(
        'identifier', metavar = 'DOOR', type = str,
        help = 'Location or ID of the door')
    subparser.add_argument(
        '--location', metavar = 'LOCATION', required = False,
        help = 'Update the location of the door')
    subparser.add_argument(
        '--controller', metavar = 'CONTROLLER', required = False,
        help = 'Update the doors controller')
    subparser.add_argument(
        '--delete-controller', action = 'store_true',
        help = 'Detach the door from its controller')
    subparser.add_argument(
        '--port', metavar = 'PORT', dest = 'index',
        type = int, required = False,
        help = 'Update the doors controller')
    subparser.add_argument(
        '--delete-port', action = 'store_true', dest = 'delete_index',
        help = 'Detach the door from its controller port')

    # Remove a door
    subparser = action_subparsers.add_parser(
        'delete', help = 'Delete a door')
    subparser.add_argument(
        'identifier', metavar = 'DOOR', type = str,
        help = 'Name or ID of the door to delete')

    # Add an access record
    subparser = action_subparsers.add_parser(
        'add-access', help = 'Add an access record to a door')
    subparser.add_argument(
        'door', metavar = 'DOOR', type = str,
        help = 'Location or ID of the door')
    group = subparser.add_mutually_exclusive_group()
    group.add_argument(
        '--user', metavar = 'USER', type = str,
        help = 'User that should have access')
    group.add_argument(
        '--group', metavar = 'GROUP', type = str,
        help = 'Group that should have access')
    subparser.add_argument(
        '--pin', metavar = 'PIN', type = str,
        help = 'PIN for this access')
    subparser.add_argument(
        '--since', metavar = 'DATE', type = str,
        help = 'Starting date for this access')
    subparser.add_argument(
        '--until', metavar = 'DATE', type = str,
        help = 'Expiration date for this access')
    subparser.add_argument(
        '--admin', action = 'store_true',
        help = 'Set this user or group as door admin')

    subparser = action_subparsers.add_parser(
        'remove-access', help = 'Remove an access record from a door')
    subparser.add_argument(
        'door', metavar = 'DOOR', type = str,
        help = 'Location or ID of the door')
    group = subparser.add_mutually_exclusive_group(required = True)
    group.add_argument(
        '--user', metavar = 'USER', type = str,
        help = 'User that should have access')
    group.add_argument(
        '--group', metavar = 'GROUP', type = str,
        help = 'Group that should have access')
    group.add_argument(
        '--pin', metavar = 'PIN', type = str,
        help = 'PIN for this access')

    args = parser.parse_args()

    # Get the class and action
    if args.cls == 'group':
        actionsClass = GroupActions
    elif args.cls == 'user':
        actionsClass = UserActions
    elif args.cls == 'controller':
        actionsClass = ControllerActions
    elif args.cls == 'door':
        actionsClass = DoorActions
    else:
        parser.print_help()
        sys.exit(1)

    action = args.action
    if action is None:
        parser.print_help()
        sys.exit(1)

    # Read the DB arguments
    db_args = {
        'db': args.db,
    }
    # These arguments can't just be None :/
    if args.db_host is not None:
        db_args['host'] = args.db_host
    if args.db_user is not None:
        db_args['user'] = args.db_user
    if args.db_password is not None:
        db_args['passwd'] = args.db_password

    # Remove them from the arguments
    del args.cls
    del args.action
    del args.db
    del args.db_host
    del args.db_user
    del args.db_password

    db = dbapi2.connect(**db_args)
    getattr(actionsClass(db), action.replace('-', '_'))(**vars(args))
