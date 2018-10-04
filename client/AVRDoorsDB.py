import DB
from datetime import datetime

class APIObject(DB.Object):
    href_format = None
    name_column = None

    def load(self, match = None, cond = None):
        'Allow loading using the object name'
        if cond is None and self.name_column is not None and \
           isinstance(match, str):
            try:
                int(match)
            except:
                cond = '%s = %%s' % self.name_column
        super(APIObject, self).load(match, cond)

    @property
    def href(self):
        return self.href_format % self.columns_value(*self.index_columns())

    created_on = DB.Column('CreatedOn', writable = False)
    last_modified = DB.Column('LastModified', writable = False)

class User(APIObject):
    table = 'Users'
    href_format = 'user/%d'
    name_column = 'UserName'

    userid = DB.Column('UserID', index = True, writable = False)
    name = DB.Column('UserName')
    email = DB.Column('EMail')
    phone = DB.Column('Phone')
    card = DB.Column('Card')
    password = DB.Column('Password')

    def __str__(self):
        email = ' <%s>' % self.email if self.email is not None else ''
        return '%s%s' % (self.name, email)

    def add_to_group(self, group, admin = False):
        if not isinstance(group, Group):
            group = Group(self._db, group)
        group.add_user(self, admin)

    def update_in_group(self, group, admin):
        if not isinstance(group, Group):
            group = Group(self._db, group)
        group.update_user(self, admin)

    def remove_from_group(self, group):
        if not isinstance(group, Group):
            group = Group(self._db, group)
        group.remove_user(self)

    def is_group_admin(self, group):
        if not isinstance(group, Group):
            group = Group(self._db, group)
        return group.is_admin(self)

    def add_access(self, door, pin = None, since = None,
                   until = None, admin = False):
        if not isinstance(door, Door):
            door = Door(self._db, door)
        door.add_access(user = self, pin = pin, since = since,
                        until = until, admin = admin)

    def update_access(self, door, pin = None, since = None,
                      until = None, admin = False):
        if not isinstance(door, Door):
            door = Door(self._db, door)
        door.update_access(
            user = self, new_pin = pin, since = since,
            until = until, admin = admin)

    def remove_access(self, door):
        if not isinstance(door, Door):
            door = Door(self._db, door)
        door.remove_access(user = self)

class Group(APIObject):
    table = 'Groups'
    href_format = 'group/%d'
    name_column = 'GroupName'

    groupid = DB.Column('GroupID', index = True, writable = False)
    name = DB.Column('GroupName')
    email = DB.Column('EMail')

    def __str__(self):
        email = ' <%s>' % self.email if self.email is not None else ''
        return '%s%s' % (self.name, email)

    def __contains__(self, user):
        """Check if a user is in the group"""
        try:
            self.get_user(user)
            return True
        except:
            return False

    def get_user(self, user):
        if not isinstance(user, User):
            user = User(self._db, user)
        return GroupUser(self._db, (self.id, user.id))

    def is_admin(self, user):
        try:
            gu = self.get_user(user)
            return gu.admin
        except:
            return False

    def add_user(self, user, admin = False):
        gu = GroupUser(self._db)
        gu.group = self
        gu.user = user
        gu.admin = admin
        gu.save()

    def update_user(self, user, admin):
        gu = self.get_user(user)
        gu.admin = admin
        gu.save()

    def remove_user(self, user):
        gu = self.get_user(user)
        gu.delete()

    def add_access(self, door, pin = None, since = None,
                   until = None, admin = False):
        if not isinstance(door, Door):
            door = Door(self._db, door)
        door.add_access(group = self, pin = pin, since = since,
                        until = until, admin = admin)

    def update_access(self, door, pin = None, since = None,
                      until = None, admin = False):
        if not isinstance(door, Door):
            door = Door(self._db, door)
        door.update_access(
            group = self, new_pin = pin, since = since,
            until = until, admin = admin)

    def remove_access(self, door):
        if not isinstance(door, Door):
            door = Door(self._db, door)
        door.remove_access(group = self)

class GroupUser(APIObject):
    table = 'GroupUsers'

    group = DB.Column('GroupID', Group, index = True)
    user = DB.Column('UserID', User, index = True)
    admin = DB.Column('GroupAdmin')

    def __str__(self):
        admin = ' (ADMIN)' if self.admin else ''
        return "%s%s" % (self.user, admin)

class Controller(APIObject):
    table = 'Controllers'
    href_format = 'controller/%d'
    name_column = 'Location'

    controller_id = DB.Column('ControllerID',
                              index = True, writable = False)
    location = DB.Column('Location')
    url = DB.Column('URL')
    username = DB.Column('Username')
    password = DB.Column('Password')
    firmware = DB.Column('Firmware')
    max_acl = DB.Column('MaxACL')

    def get_door(self, index):
        cursor = self._db.cursor()
        cursor.execute("select DoorID from Doors " +
                       "where ControllerID = %s and DoorIndex = %s",
                       (self.id, index))
        if cursor.rowcount == 0:
            raise IndexError("No door with index %s" % index)
        did, = cursor.fetchone()
        return Door(self._db, did)

class Door(APIObject):
    table = 'Doors'
    href_format = 'door/%d'
    name_column = 'Location'

    doorid = DB.Column('DoorID', index = True, writable = False)
    location = DB.Column('Location')
    controller = DB.Column('ControllerID', Controller)
    index = DB.Column('DoorIndex')

    def get_access(self, user = None, group = None, pin = None):
        if user is None and group is None and pin is None:
            raise ValueError("No user, group or PIN given for access")
        if user is not None and group is not None:
            raise ValueError("Both user and group given for access")
        if user is not None and not isinstance(user, User):
            user = User(self._db, user)
        if group is not None and not isinstance(group, Group):
            group = Group(self._db, group)
        if user is not None or group is not None:
            uid = user.id if user is not None else None
            gid = group.id if group is not None else None
            return DoorAccess(self._db, (self.id, uid, gid),
                              'DoorID = %s and UserID <=> %s ' +
                              'and GroupID <=> %s')
        else:
            return DoorAccess(self._db, (self.id, pin),
                              'DoorID = %s and UserID is NULL ' +
                              'and GroupID is NULL and PIN = %s')

    def add_access(self, user = None, group = None,
                   pin = None, since = None, until = None, admin = False):
        try:
            self.get_access(user, group, pin)
        except ValueError:
            pass
        else:
            raise ValueError('This access permission already exists')
        if user is not None and not isinstance(user, User):
            user = User(self._db, user)
        if group is not None and not isinstance(group, Group):
            group = Group(self._db, group)
        # PIN only can't be admin
        if user is None and group is None:
            admin = False
        a = DoorAccess(self._db)
        a.door = self
        a.user = user
        a.group = group
        a.pin = pin
        a.since = since
        a.until = until
        a.admin = admin
        a.save()

    def update_access(self, user = None, group = None, pin = None,
                      new_pin = None, since = None, until = None,
                      admin = False):
        a = self.get_access(user, group, pin)
        # PIN only can't be admin
        if a.user is None and a.group is None:
            admin = False
        a.pin = new_pin
        a.since = since
        a.until = until
        a.admin = admin
        a.save()

    def remove_access(self, user = None, group = None, pin = None):
        a = self.get_access(user, group, pin)
        a.delete()

    def get_user_access(self, user):
        return AllAccess(self._db, (self.id, user.id),
                         "DoorID = %s and UserID = %s")

    def is_admin(self, user):
        try:
            access = self.get_user_access(user)
        except ValueError:
            return False
        return access.admin

class Access(object):
    def valid(self):
        return (self.since is None or self.since <= datetime.now()) and \
            (self.until is None or self.until > datetime.now())

    def describe_condition(self):
        desc = ""
        if self.pin is not None:
            desc += " with PIN %s" % self.pin
        if self.since is not None:
            if self.since <= datetime.now():
                desc += ", started %s" % self.since
            else:
                desc += ", starting %s" % self.since
        if self.until is not None:
            if self.until <= datetime.now():
                desc += ", expired since %s" % self.until
            else:
                desc += ", until %s" % self.until
        if self.admin:
            desc += " (ADMIN)"
        return desc

    def describe_to(self):
        return self.door.location + self.describe_condition()

    def __str__(self):
        return self.describe_to()

class AllAccess(DB.Object, Access):
    table = 'AllAccess'

    door = DB.Column('DoorID', Door, index = True, writable = False)
    user = DB.Column('UserID', User, index = True, writable = False)
    pin = DB.Column('PIN', index = True)
    since = DB.Column('Since')
    until = DB.Column('Until')
    admin = DB.Column('DoorAdmin')

class DoorAccess(APIObject, Access):
    table = 'DoorAccess'
    href_format = 'door-access/%d'

    door_access_id = DB.Column('DoorAccessID',
                               index = True, writable = False)
    door = DB.Column('DoorID', Door)
    user = DB.Column('UserID', User)
    group = DB.Column('GroupID', Group)
    pin = DB.Column('PIN')
    since = DB.Column('Since')
    until = DB.Column('Until')
    admin = DB.Column('DoorAdmin')

    def describe_condition(self):
        desc = super().describe_condition()
        if self.admin:
            desc += " (ADMIN)"
        return desc

    def describe_who(self):
        if self.user is not None:
            desc = "%s" % self.user.name
        elif self.group is not None:
            desc = "%s group" % self.group.name
        else:
            desc = "All"
        return desc + self.describe_condition()

User.groups = DB.List(GroupUser)
User.access = DB.List(AllAccess)
Group.users = DB.List(GroupUser)

Controller.doors = DB.List(Door)

User.doors = DB.List(DoorAccess)
Group.doors = DB.List(DoorAccess)
Door.access = DB.List(DoorAccess)


if __name__ == '__main__':
    import MySQLdb as dbapi2
    db = dbapi2.connect(db='AVRDoors')
    u = User(db, 1)
    print(u.href)
    for gu in u.groups:
        print("%s" % (gu.group.name,))
    print()
    g = Group(db, 1)
    print(g.href)
    for a in g.doors:
        print("%s" % a.door.location)
    print()
    d = Door(db, 1)
    print(d.href)
    for a in d.access:
        print(a.describe_who())
