
class Column(object):
    def __init__(self, name, data_type = None,
                 writable = True, index = False):
        self.name = name
        self.data_type = data_type
        self.writable = writable
        self.index = index
        self.values = {}

    def encode(self, obj, val):
        if val is not None and self.data_type is not None:
            if hasattr(self.data_type, 'encode'):
                val = self.data_type.encode(obj, val)
            else:
                val = self.data_type(val)
        return val

    def decode(self, obj, val):
        if val is not None and self.data_type is not None:
            if hasattr(self.data_type, 'decode'):
                val = self.data_type.decode(obj, val)
        return val

    def set_raw_value(self, obj, val):
        self.values[obj] = val

    def get_raw_value(self, obj):
        return self.values[obj]

    def isset(self, obj):
        return obj in self.values

    def __get__(self, obj, cls = None):
        if obj is None:
            return self
        return self.decode(obj, self.values[obj])

    def __set__(self, obj, val):
        if not self.writable:
            raise AttributeError("column is read-only")
        self.values[obj] = self.encode(obj, val)

    def __delete__(self, obj):
        if obj in self.values:
            del self.values[obj]

class List(object):
    def __init__(self, *args):
        self.list_args = args
        self.lists = {}

    def __get__(self, obj, cls = None):
        if obj is None:
            return self
        if obj not in self.lists:
            self.lists[obj] = ObjectList(obj, *self.list_args)
        return self.lists[obj]

    def __set__(self, obj, value):
        raise AttributeError("can't set attribute")

    def __delete__(self, obj):
        if obj in self.lists:
            del self.lists[obj]

class ObjectList(object):
    def __init__(self, obj, itemClass, list_table = None):
        if list_table is None:
            list_table = itemClass.table
        self._obj = obj
        self._refs = None
        self._instances = {}
        self._list_table = list_table
        self._itemClass = itemClass
        self.load_items()

    def load_items(self):
        itemClass = self._itemClass
        obj = self._obj
        cursor = self._obj._db.cursor()
        query = "select %s from %s where %s" % (
            itemClass.columns_name(*itemClass.index_columns()),
            self._list_table, obj.columns_condition(*obj.index_columns()))
        cursor.execute(query, obj.match_value(obj.id))
        self._refs = list(cursor)

    def __getitem__(self, key):
        if key not in self._instances:
            self._instances[key] = self._itemClass(
                self._obj._db, self._refs[key])
        return self._instances[key]

    def __len__(self):
        return len(self._refs)

class Object(object):
    table = None

    def __init__(self, db, oid = None, cond = None):
        self._db = db
        self._id = None
        self._exists = False
        if oid != None:
            self.load(oid, cond)

    @classmethod
    def columns(cls):
        attrs = (getattr(cls, n) for n in dir(cls)
                 if not n.startswith('_'))
        return tuple((a for a in attrs if isinstance(a, Column)))

    @classmethod
    def index_columns(cls):
        columns = (c for c in cls.columns() if c.index)
        columns = sorted(columns, key = lambda c: c.name)
        return tuple(columns)

    @classmethod
    def writable_columns(cls):
        return tuple((c for c in cls.columns() if c.writable))

    def save_columns(self):
        return tuple((c for c in self.writable_columns() if c.isset(self)))

    @classmethod
    def columns_name(cls, *columns):
        return ", ".join((c.name for c in columns))

    @classmethod
    def columns_condition(cls, *columns):
        return " and ".join(("%s <=> %%s" % c.name for c in columns))

    @staticmethod
    def match_value(value):
        return value if isinstance(value, (tuple, list)) else (value,)

    def columns_value(self, *columns):
        return tuple((c.__get__(self, type(self)) for c in columns))

    def columns_raw_value(self, *columns):
        return tuple((c.get_raw_value(self) for c in columns))

    @property
    def id(self):
        vals = self.columns_raw_value(*self.index_columns())
        return vals if len(vals) > 1 else vals[0]

    def clear(self):
        for c in self.columns():
            c.__delete__(self)
        # TODO: Also remove the lists?
        self._exists = False

    def load(self, match = None, cond = None):
        # Clear the values already stored
        self.clear()
        # If no match is given use the object ID
        if match is None:
            match = self.id
        # If no condition is given match the index columns
        if cond is None:
            cond = self.columns_condition(*self.index_columns())
        # Get the data from the DB
        columns = self.columns()
        cursor = self._db.cursor()
        query = "select %s from %s where %s" % (
            self.columns_name(*columns), self.table, cond)
        cursor.execute(query, self.match_value(match))
        if cursor.rowcount == 0:
            # TODO: Add a custom exception here
            raise ValueError("Object Not Found")
        if cursor.rowcount > 1:
            # TODO: Add a custom exception here
            raise ValueError("Too many matching object")
        # Store the values
        values = cursor.fetchone()
        for i, c in enumerate(columns):
            c.set_raw_value(self, values[i])
        self._exists = True

    @classmethod
    def delete_objects(cls, db, match, cond = None):
        if cond is None:
            cond = cls.columns_condition(*cls.index_columns())
        cursor = db.cursor()
        query = "delete from %s where %s" % (cls.table, cond)
        cursor.execute(query, cls.match_value(match))
        db.commit()

    def delete(self):
        self.delete_objects(self._db, self.id)

    def save(self):
        columns = self.save_columns()
        sets = ", ".join(("%s = %%s" % c.name for c in columns))
        args = self.columns_raw_value(*columns)
        cursor = self._db.cursor()
        if not self._exists:
            query = "insert into %s set %s" % (self.table, sets)
            cursor.execute(query, args)
            try:
                oid = self.id
            except:
                oid = cursor.lastrowid
        else:
            cond = self.columns_condition(*self.index_columns())
            query = "update %s set %s where %s" % (self.table, sets, cond)
            args += self.match_value(self.id)
            cursor.execute(query, args)
            oid = self.id
        self._db.commit()
        self.load(oid)

    @classmethod
    def encode(cls, obj, val):
        # If the value is not an instance of this class
        # use decode to attempt a lookup.
        if not isinstance(val, cls):
            val = cls.decode(obj, val)
        return val.id

    @classmethod
    def decode(cls, obj, val):
        return cls(obj._db, val)

    @classmethod
    def _get_from_db(cls, db, order_by = None, where = None, match = None):
        if order_by is None:
            order_by = cls.columns_name(*cls.index_columns())
        where = ' where %s' % where if where is not None else ''
        match = () if match is None else cls.match_value(match)
        query = "select %s from %s%s order by %s" % (
            cls.columns_name(*cls.index_columns()),
            cls.table, where, order_by)
        cursor = db.cursor()
        cursor.execute(query, match)
        return cursor

    @classmethod
    def get_all(cls, db, *args, **kwargs):
        cursor = cls._get_from_db(db, *args, **kwargs)
        return [ cls(db, r) for r in cursor ]

    @classmethod
    def get_one(cls, db, *args, **kwargs):
        cursor = cls._get_from_db(db, *args, **kwargs)
        row = cursor.fetchone()
        if row is None:
            # TODO: Add a custom exception here
            raise ValueError("Object Not Found")
        return cls(db, row)
